#include "native-language-detector.hpp"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <functional>

namespace {
using Numbers = QHash<QString, double>;
QRegularExpression rx(const QString &pattern, bool insensitive = false)
{
 QRegularExpression result(pattern, insensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
 if (!result.isValid()) throw std::runtime_error(("Invalid native expression: " + pattern).toStdString());
 return result;
}
QString fold(QString text) { return text.normalized(QString::NormalizationForm_D).remove(rx(QStringLiteral("\\p{M}+"))); }
QStringList strings(const QJsonValue &value)
{
 QStringList output;
 if (value.isString()) output.append(value.toString());
 else for (const auto &entry : value.toArray()) output.append(entry.toString());
 return output;
}
QSet<QString> setOf(const QJsonValue &value) { const auto list = strings(value); return QSet<QString>(list.begin(), list.end()); }
QJsonObject readObject(const QString &path, qint64 limit)
{
 QFile file(path);
 if (!file.open(QIODevice::ReadOnly) || file.size() > limit || file.size() < 2) throw std::runtime_error("Missing or oversized language data");
 QJsonParseError error;
 const auto document = QJsonDocument::fromJson(file.readAll(), &error);
 if (error.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid language data JSON");
 return document.object();
}
int matches(const QString &text, const QRegularExpression &pattern)
{
 int count = 0; auto it = pattern.globalMatch(text); while (it.hasNext()) { it.next(); ++count; } return count;
}
QString mapMatches(const QString &text, const QRegularExpression &pattern, const std::function<QString(const QString &)> &transform)
{
 QString out; qsizetype cursor = 0; auto it = pattern.globalMatch(text);
 while (it.hasNext()) { const auto m = it.next(); out += text.mid(cursor, m.capturedStart() - cursor); out += transform(m.captured()); cursor = m.capturedEnd(); }
 return out + text.mid(cursor);
}
QString scriptOf(char32_t point)
{
 // Cache by Unicode script enum, not by character, to bound memory.
 static thread_local QHash<int, QString> cache;
 const int script = int(QChar::script(point));
 auto found = cache.constFind(script); if (found != cache.cend()) return found.value();
 static const QStringList names = QStringLiteral("Latin Cyrillic Greek Arabic Hebrew Devanagari Bengali Kannada Malayalam Sinhala Oriya Myanmar Khmer Lao Ethiopic Tibetan Thaana Canadian_Aboriginal Tamil Telugu Gujarati Gurmukhi Hangul Hiragana Katakana Han Thai Armenian Georgian Cherokee Syriac Tifinagh Yi Nko Vai Ol_Chiki Chakma Meetei_Mayek Adlam").split(' ');
 const QString ch = QString::fromUcs4(&point, 1);
 QString name = QStringLiteral("Other");
 for (const auto &candidate : names) if (rx("\\p{" + candidate + "}").match(ch).hasMatch()) { name = candidate; break; }
 cache.insert(script, name); return name;
}
struct Scripts { int letters = 0; QHash<QString,int> counts; QString dominant; };
Scripts scriptsOf(const QString &text)
{
 Scripts s; int maximum = 0;
 for (const auto point : text.toUcs4()) {
  if (!QChar::isLetter(point)) continue;
  ++s.letters; const auto script = scriptOf(point); const int count = ++s.counts[script];
  if (count > maximum) { maximum = count; s.dominant = script; }
 }
 return s;
}
double compatibility(const QStringList &names, const Scripts &s)
{
 if (!s.letters) return 1;
 double best = 0.02;
 for (const auto &name : names) {
  double score; const int kana = s.counts.value("Hiragana") + s.counts.value("Katakana");
  const int same = s.counts.value(name); const double ratio = double(same) / s.letters;
  if (name == "Japanese") score = kana ? 1 : s.counts.value("Han") ? 0.55 : 0.02;
  else if (name == "Han") score = kana ? 0.2 : s.counts.value("Han") ? 1 : 0.02;
  else if (name == "Latin") score = same == 0 && s.letters > 1 ? 0.02 : std::max(0.15, ratio);
  else score = same ? std::max(0.2, ratio) : 0.02;
  best = std::max(best, score);
 }
 return best;
}
struct Candidate { int index = 0; double ml = 0, hint = 0, distinct = 0, evidence = 0, ortho = 0, script = 0, score = 0, confidence = 0; };
bool reliable(const Candidate &a, const Candidate &b, int useful, int tokens)
{
 if (useful < 4 || (useful < 7 && a.confidence < 0.88)) return false;
 const double gap = a.confidence - b.confidence;
 if (tokens <= 2 && useful < 12 && gap < 0.18) return false;
 const bool margin = a.confidence >= .62 && gap >= .35 && useful >= 12;
 const bool lexical = useful >= 10 && tokens >= 3 && a.confidence >= .2 && a.hint >= .66 && a.hint - b.hint >= .25;
 const bool ortho = useful >= 10 && a.confidence >= .25 && a.ortho >= .66 && a.ortho - b.ortho >= .5;
 const bool combined = useful >= 12 && tokens >= 4 && a.confidence >= .4 && gap >= .25 && a.hint >= .66 && a.ortho >= .5;
 const bool model = useful >= 12 && tokens >= 3 && a.confidence >= .4 && gap >= .24 && a.script >= .8 && a.score - b.score >= .075 && (a.ml - b.ml >= .07 || (a.distinct >= .7 && a.distinct - b.distinct >= .18));
 const bool rank = useful >= 10 && tokens >= 3 && a.script >= .8 && a.score - b.score >= .04 && a.ml - b.ml >= .02;
 const bool contrast = useful >= 12 && tokens >= 4 && a.confidence >= .32 && a.hint >= .66 && a.evidence >= 1.25 && a.evidence - b.evidence >= .45 && a.script >= .8;
 const bool evidence = lexical || ortho || combined || model || rank || contrast;
 if (a.confidence < .72 && !margin && !evidence) return false;
 if (gap < .08 && a.confidence < .9 && !evidence) return false;
 return true;
}
}

struct NativeLanguageDetector::Data {
 struct Hint { QString raw, folded; };
 struct Language { QString code, script, family; QStringList scripts, ortho; QVector<Hint> hints; bool noSpace = false; };
 QVector<Language> languages;
 QHash<QString, QVector<QPair<int,double>>> inverted;
 Numbers idf;
 QHash<QString,int> hintFrequency;
 QSet<QString> common, distinctive, chat;
 QJsonObject aliases, confusables, leet, uwu;
 QJsonObject sourceModel, metadata;
 QSet<QString> ignored;
 int minN = 2, maxN = 4;
 QString normalize(QString text, const QJsonArray &knownEmotes) const;
 double english(const QString &text, const Scripts &scripts) const;
};

NativeLanguageDetector::NativeLanguageDetector(const QString &directory) : data_(std::make_unique<Data>())
{
 auto &d = *data_;
 QJsonObject meta, model;
 if (QFileInfo(directory).isDir()) {
  meta = readObject(QDir(directory).filePath("metadata.json"), 4 * 1024 * 1024);
  model = readObject(QDir(directory).filePath("model.json"), 32 * 1024 * 1024);
 } else {
  const auto imported = readObject(directory, 40 * 1024 * 1024);
  if (imported.value("format").toString() != "bh-native-model" || imported.value("formatVersion").toInt() != 1 || !imported.value("model").isObject() || !imported.value("metadata").isObject()) throw std::runtime_error("Expected a Bacons Helper native model export (.json)");
  model = imported.value("model").toObject(); meta = imported.value("metadata").toObject();
 }
 if (QJsonDocument(meta).toJson(QJsonDocument::Compact).size() > 4*1024*1024) throw std::runtime_error("Oversized metadata");
 // Validate tunable tables before they can expand text or amplify work.
 for(const QString &name:QStringList{"confusables","leet","uwu"}){
  const auto table=meta.value(name).toObject();if(table.size()>4096)throw std::runtime_error("Oversized normalization table");
  for(auto it=table.begin();it!=table.end();++it)if(it.key().isEmpty()||it.key().size()>128||!it.value().isString()||it.value().toString().size()>128)throw std::runtime_error("Invalid normalization entry");
 }
 for(const QString &name:QStringList{"wordHints","orthographicHints"}){
  const auto table=meta.value(name).toObject();int total=0;if(table.size()>512)throw std::runtime_error("Oversized hint table");
  for(auto it=table.begin();it!=table.end();++it){
   if(!it.value().isArray()||it.value().toArray().size()>4096)throw std::runtime_error("Invalid language hints");
   for(const auto &value:it.value().toArray()){++total;if(!value.isString()||value.toString().isEmpty()||value.toString().size()>128)throw std::runtime_error("Invalid word hint");}
  }
  if(total>131072)throw std::runtime_error("Too many hints");
 }
 d.sourceModel = model; d.metadata = meta;
 const auto ignored = meta.value("ignoredTextHashes").toArray();
 if (ignored.size() > 1000) throw std::runtime_error("Too many ignored phrases");
 for (const auto &value : ignored) {
  if (!value.isString() || !rx("^[a-f0-9]{64}$").match(value.toString()).hasMatch()) throw std::runtime_error("Invalid ignored phrase hash");
  d.ignored.insert(value.toString());
 }
 if (meta.value("schema").toInt() != 1 || (model.value("version").toInt() != 1 && model.value("version").toInt() != 2)) throw std::runtime_error("Unsupported language data version");
 const auto ngrams = model.value("ngrams").toObject();
 d.minN = ngrams.value("min").toInt(2); d.maxN = ngrams.value("max").toInt(4);
 if (d.minN < 1 || d.maxN < d.minN || d.maxN > 8) throw std::runtime_error("Invalid ngram range");
 auto numeric = [](const QJsonValue &value) {
  const double number = value.toDouble(-1);
  if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > 1000000) throw std::runtime_error("Invalid model weight");
  return number;
 };
 if (!model.value("languages").isObject() || !meta.value("languages").isObject() || !model.value("ngrams").isObject() || !model.value("idf").isObject()) throw std::runtime_error("Missing model fields");
 const auto idf = model.value("idf").toObject();
 if (idf.size()>1000000) throw std::runtime_error("Oversized feature table");
 for (auto it = idf.begin(); it != idf.end(); ++it) d.idf.insert(it.key(), numeric(it.value()));
 d.aliases = meta.value("aliases").toObject(); d.confusables = meta.value("confusables").toObject(); d.leet = meta.value("leet").toObject(); d.uwu = meta.value("uwu").toObject();
 const auto english = meta.value("english").toObject();
 d.common = setOf(english.value("common")); d.distinctive = setOf(english.value("distinctive")); d.chat = setOf(english.value("chat"));
 const auto registry = meta.value("languages").toObject(); const auto profiles = model.value("languages").toObject();
 if (profiles.isEmpty() || profiles.size() > 512) throw std::runtime_error("Invalid language profile count");
 const auto hints = meta.value("wordHints").toObject(); const auto orthography = meta.value("orthographicHints").toObject();
 const QSet<QString> noSpaceLanguages = {"zh","ja","th","my","km","lo","yue"};
 const QSet<QString> noSpaceScripts = {"Japanese","Han","Thai","Myanmar","Khmer","Lao","Tibetan","Yi","Chakma","Meetei_Mayek"};
 // Word-hint frequency is measured over all registry languages, as in 0.11.3.
 for (auto it = hints.begin(); it != hints.end(); ++it) {
  QSet<QString> unique;
  for (const auto &word : strings(it.value())) unique.insert(noSpaceLanguages.contains(it.key()) ? word : fold(word));
  for (const auto &word : unique) ++d.hintFrequency[word];
 }
 // Explicit order preserves JavaScript tie-breaking (JSON objects in Qt sort keys).
 const auto order = meta.value("modelOrder").toArray();
 QStringList codes = strings(order); if (codes.isEmpty()) codes = profiles.keys();
 QSet<QString> uniqueCodes(codes.begin(), codes.end());
 if (uniqueCodes.size()!=codes.size() || codes.size()!=profiles.size()) throw std::runtime_error("Language order must name each profile once");
 for (const auto &code : codes) {
  if (!rx("^[a-z0-9-]{1,32}$").match(code).hasMatch()) throw std::runtime_error("Invalid language code");
  if (!profiles.contains(code) || !registry.contains(code)) throw std::runtime_error("Missing language metadata");
  const auto info = registry.value(code).toObject(); Data::Language language;
  language.code = code; language.script = info.value("script").toString(); language.family = info.value("family").toString();
  language.scripts = info.contains("scripts") ? strings(info.value("scripts")) : QStringList{language.script};
  language.noSpace = noSpaceLanguages.contains(code);
  for (const auto &script : language.scripts) if (noSpaceScripts.contains(script)) language.noSpace = true;
  language.ortho = strings(orthography.value(code));
  for (const auto &word : strings(hints.value(code))) language.hints.append({word, noSpaceLanguages.contains(code) ? word : fold(word)});
  const int index = d.languages.size(); d.languages.append(language);
  if (!profiles.value(code).isObject() || !profiles.value(code).toObject().value("vector").isObject()) throw std::runtime_error("Missing language vector");
  const auto vector = profiles.value(code).toObject().value("vector").toObject();
  if (vector.isEmpty()) throw std::runtime_error("Empty language vector");
  if (vector.size() > 100000) throw std::runtime_error("Oversized language profile");
  double magnitude=0;
  for (auto it = vector.begin(); it != vector.end(); ++it) {
   const auto length=it.key().toUcs4().size();
   if(length<d.minN || length>d.maxN) throw std::runtime_error("Invalid ngram key");
   const double weight=numeric(it.value()); magnitude+=weight*weight;
   d.inverted[it.key()].append({index, weight});
  }
  if(std::abs(magnitude-1.0)>0.02) throw std::runtime_error("Language vectors must be normalized");
 }
 if (d.common.isEmpty() || d.inverted.isEmpty()) throw std::runtime_error("Incomplete language data");
}
NativeLanguageDetector::~NativeLanguageDetector() = default;

QString NativeLanguageDetector::Data::normalize(QString text, const QJsonArray &emotes) const
{
 text = text.normalized(QString::NormalizationForm_KC);
 text.remove(rx(QStringLiteral("[\\x{061C}\\x{200B}-\\x{200F}\\x{202A}-\\x{202E}\\x{2060}-\\x{2064}\\x{2066}-\\x{2069}\\x{FEFF}]")));
 text.replace(rx(QStringLiteral("[\\x{0000}-\\x{0008}\\x{000B}\\x{000C}\\x{000E}-\\x{001F}\\x{007F}]")), " ");
 text.replace(rx(QStringLiteral("\\[[^\\]\\r\\n]*?\\bGIF\\s+by\\s+[^\\]\\r\\n]+\\]"), true), " ");
 const QStringList internet = {
  R"(\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,63}\b)",
  R"(\b(?:[a-z][a-z0-9+.-]{1,31}://|www\.)[^\s<>()]+)",
  R"(\b(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#][^\s<>()]*)?)",
  R"(\blocalhost(?::\d{1,5})?(?:[/?#][^\s<>()]*)?)",
  R"(\b(?:[a-z0-9](?:[a-z0-9-]{0,62})\.)+(?:[a-z]{2,63}|xn--[a-z0-9-]{2,59})(?::\d{1,5})?(?:[/?#][^\s<>()]*)?)",
  R"((^|\s)[@#][A-Z0-9_][A-Z0-9_.-]*)",
  R"(<a?:[A-Za-z0-9_]{2,32}:\d{2,24}>)", R"(:[A-Za-z0-9][A-Za-z0-9_+\-]{1,63}:)"};
 for (const auto &pattern : internet) text.replace(rx(pattern, true), " ");
 // Remove emoji before character/script scoring. PCRE2 properties are provided by Qt.
 text.replace(rx(R"((?:\p{Extended_Pictographic}|\p{Emoji_Presentation}|\p{Emoji_Modifier}|\x{FE0F}|\x{200D})+)"), " ");
 QSet<QString> known;
 for (const auto &v : emotes) if (known.size() < 4096 && v.isString() && v.toString().size() <= 128) known.insert(v.toString().normalized(QString::NormalizationForm_KC));
 const auto emoticon = rx(R"(^(?:<\/?3|[xX][dD]|[dD][:=;8]|[Tt][_-][Tt]|[uU][_-][uU]|[oO0][_-][oO0]|\^[_.'-]?\^|[-^][_.][-^]|[<>]?[=:;8xX][\-oO*'^]?[)(\][dDpP/\\:}{@|]+|[)(\]][\-oO*'^]?[=:;8xX][<>]?)$)");
 QStringList tokens;
 for (auto token : text.split(rx("\\s+"), Qt::SkipEmptyParts)) {
  const auto trimmed = QString(token).remove(rx(R"(^[^\p{L}\p{M}\p{N}_+\-]+|[^\p{L}\p{M}\p{N}_+\-]+$)"));
  if (known.contains(token) || known.contains(trimmed) || emoticon.match(token).hasMatch()) continue;
  if (rx(QStringLiteral("[ಠಥツ◕ノヽづงᕙᕗʘ益ヮ︵┻┳╯╰]")).match(token).hasMatch()) {
   int letters = 0, symbols = 0;
   for (char32_t cp : token.toUcs4()) if (QChar::isLetterOrNumber(cp)) ++letters; else if (!QChar::isSpace(cp)) ++symbols;
   if ((symbols > 0 && symbols >= letters) || token == QStringLiteral("ಠ_ಠ") || token == QStringLiteral("ಥ_ಥ") || token == QStringLiteral("ಠ益ಠ") || token == QStringLiteral("ಥ﹏ಥ")) continue;
  }
  bool ascii = false, mapped = false, unmapped = false; QSet<QString> scripts;
  for (char32_t cp : token.toUcs4()) {
   if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) ascii = true;
   if (!QChar::isLetter(cp)) continue;
   const QString script = scriptOf(cp); scripts.insert(script);
   if (script != "Latin") { const QString ch = QString::fromUcs4(&cp,1); if (confusables.contains(ch)) mapped = true; else unmapped = true; }
  }
  if (scripts.size() > 1 && ascii && mapped && !unmapped) {
   QString normalized;
   for (char32_t cp : token.toUcs4()) { const QString ch = QString::fromUcs4(&cp,1); normalized += confusables.value(ch).toString(ch); }
   token = normalized;
  }
  tokens.append(token);
 }
 text = tokens.join(' ');
 text = mapMatches(text, rx(R"(\b[\p{L}0-9@$]{3,}\b)"), [this](const QString &token) {
  int letters = 0, replaceable = 0; const auto points = token.toUcs4();
  for (char32_t cp : points) { if (QChar::isLetter(cp)) ++letters; if (leet.contains(QString::fromUcs4(&cp,1))) ++replaceable; }
  if (!letters || !replaceable || replaceable > (points.size()+1)/2) return token;
  QString result; for (char32_t cp : points) { const auto ch = QString::fromUcs4(&cp,1); result += leet.value(ch).toString(ch); } return result;
 });
 text = mapMatches(text, rx(R"(\b[\p{L}']+\b)"), [this](const QString &word) { return uwu.value(word.toLower()).toString(word); });
 return text.replace(rx(R"([^\p{L}\p{M}\p{N}'’\- ]+)"), " ").simplified().toLower();
}

double NativeLanguageDetector::Data::english(const QString &text, const Scripts &scripts) const
{
 const auto tokensRx = rx(R"([\p{L}\p{M}]+(?:'[\p{L}\p{M}]+)?)");
 auto it = tokensRx.globalMatch(QString(text).replace(QChar(0x2019), '\''));
 int count = 0, recognized = 0, distinctiveCount = 0, chats = 0, suffixes = 0;
 const auto suffix = rx(R"((?:ing|ed|ly|ness|ment|tion|sion|able|ible|less|ful|ous|ize|ise|er|est)$)", true);
 while (it.hasNext()) {
  const QString token = it.next().captured(); if (token.size() < 2 && token != "i" && token != "u" && token != "r") continue;
  ++count; const QString collapsed = QString(token).replace(rx(R"(([a-z])\1{2,})", true), "\\1");
  const QStringList variants{token, QString(token).remove('\''), collapsed, QString(collapsed).remove('\'')};
  bool a = false, b = false, c = false, s = false;
  for (const auto &v : variants) { a |= common.contains(v); b |= distinctive.contains(v); c |= chat.contains(v); s |= v.size() >= 5 && suffix.match(v).hasMatch(); }
  recognized += a; distinctiveCount += b; chats += c; suffixes += s;
 }
 if (!count) return 0;
 const int contractions = matches(text, rx(R"(\b(?:i'm|i've|i'll|i'd|you're|you've|you'll|you'd|we're|we've|we'll|we'd|they're|they've|they'll|they'd|he's|he'll|he'd|she's|she'll|she'd|it's|that's|there's|what's|who's|can't|cannot|won't|wouldn't|shouldn't|couldn't|don't|doesn't|didn't|isn't|aren't|wasn't|weren't|haven't|hasn't|hadn't)\b)", true));
 double score = std::min(.46, double(recognized)/count*.62) + std::min(.2, double(distinctiveCount)/count*.55) + std::min(.18, double(chats)/count*.6) + std::min(.13, contractions*.08) + std::min(.1, double(suffixes)/count*.3);
 if (scripts.letters && double(scripts.counts.value("Latin"))/scripts.letters > .96) score += .04;
 if (count >= 3 && recognized >= 2) score += .08;
 if (count >= 5 && recognized >= 3) score += .07;
 const double accentRatio = scripts.letters ? double(matches(text,rx(QStringLiteral("[àáâãäåæçèéêëìíîïñòóôõöøœùúûüýÿāăąćčďęěğīłńňőřśşšţťūůűźżž]"),true)))/scripts.letters : 0;
 if (accentRatio > .12) score -= std::min(.22, accentRatio*.75);
 if (count == 1 && !recognized && !chats && !contractions) score = std::min(score,.2);
 return std::clamp(score,0.0,1.0);
}

QJsonObject NativeLanguageDetector::detect(const QJsonObject &job) const
{
 QElapsedTimer timer; timer.start(); const auto &d = *data_;
 if (!job.value("id").isString() || job.value("id").toString().size() > 64 || !job.value("text").isString() || job.value("text").toString().size() > 2000) throw std::runtime_error("Invalid detection job");
 QString target = job.value("target").toString("en").trimmed().toLower().replace('_','-');
 auto knownLanguage = [&d](const QString &code) { return std::any_of(d.languages.begin(),d.languages.end(),[&](const auto &v){return v.code==code;}); };
 const QString primary = target.section('-',0,0);
 if (knownLanguage(primary)) target = primary;
 else target = d.aliases.value(target).toString(d.aliases.value(primary).toString());
 if (!knownLanguage(target)) throw std::runtime_error("Unsupported target language");
 const QString text = d.normalize(job.value("text").toString(),job.value("knownEmotes").toArray());
 if(text.size()>8000)throw std::runtime_error("Normalized input is too long");
 const Scripts scripts = scriptsOf(text);
 const int useful = matches(text,rx(R"([\p{L}\p{M}])"));
 const auto tokenList = text.split(' ',Qt::SkipEmptyParts); const int tokens = tokenList.size();
 const int words = matches(text,rx(R"(\p{Han}|[\p{Hiragana}\p{Katakana}]|\p{L}[\p{L}\p{M}]*)"));
 QJsonObject result{{"id",job.value("id")},{"target",target},{"text",job.value("text")},{"normalized",text},{"_generation",job.value("_generation")},{"backend","native-cpp"},{"modelVersion","0.11.3"}};
 if (useful < 4) {
  result.insert("language","unknown"); result.insert("confidence",0); result.insert("automationReliable",false); result.insert("translate",false); result.insert("reason","insufficient-evidence"); return result;
 }
 const auto points = ("_" + QString(text).replace(' ','_') + "_").toStdU32String();
 Numbers vector; double norm = 0;
 for (int n = d.minN; n <= d.maxN; ++n) for (qsizetype i = 0; i+n <= points.size(); ++i) ++vector[QString::fromUcs4(points.data()+i,n)];
 for (auto it = vector.begin(); it != vector.end(); ++it) { it.value() *= d.idf.value(it.key(),1); norm += it.value()*it.value(); }
 norm = std::sqrt(norm); if (!norm) norm = 1;
 QVector<double> ml(d.languages.size(),0);
 for (auto it = vector.begin(); it != vector.end(); ++it) {
  const auto found = d.inverted.constFind(it.key()); if (found == d.inverted.cend()) continue;
  const double value = it.value()/norm; for (const auto &entry : found.value()) ml[entry.first] += value*entry.second;
 }
 QSet<QString> tokenSet(tokenList.begin(),tokenList.end()), folded;
 for (const auto &token : tokenList) folded.insert(fold(token));
 QVector<Candidate> candidates; QVector<double> hintScores, orthoScores, evidenceScores;
 for (const auto &language : d.languages) {
  int hits = 0, orthoHits = 0; double weighted = 0; QSet<QString> matched;
  for (const auto &hint : language.hints) {
   const bool match = language.noSpace ? text.contains(hint.raw) : tokenSet.contains(hint.raw) || folded.contains(hint.folded);
   if (!match) continue; ++hits;
   if (!matched.contains(hint.folded)) { matched.insert(hint.folded); weighted += 1/std::sqrt(std::max(1,d.hintFrequency.value(hint.folded,1))); }
  }
  for (const auto &hint : language.ortho) if (text.contains(hint)) ++orthoHits;
  hintScores.append(std::min(1.0,hits/3.0)); orthoScores.append(std::min(1.0,orthoHits/2.0)); evidenceScores.append(std::min(2.0,weighted/2.5));
 }
 int yue = -1; for (int i=0;i<d.languages.size();++i) if(d.languages[i].code=="yue") yue=i;
 for (int i=0;i<d.languages.size();++i) {
  const auto &language = d.languages[i]; Candidate c; c.index=i; c.ml=std::max(0.0,ml[i]); c.hint=hintScores[i]; c.evidence=evidenceScores[i]; c.distinct=std::min(1.0,c.evidence); c.ortho=orthoScores[i]; c.script=compatibility(language.scripts,scripts);
  double bonus = language.script != "Latin" && c.script > .7 ? .12 : 0;
  const int kana = scripts.counts.value("Hiragana")+scripts.counts.value("Katakana");
  if(language.code=="ja" && kana) bonus+=.22;
  if(language.code=="ko" && scripts.counts.value("Hangul")) bonus+=.2;
  if(language.code=="zh" && scripts.counts.value("Han") && !kana && (yue<0 || std::max(hintScores[yue],orthoScores[yue])<.33)) bonus+=.12;
  if(language.code=="yue" && scripts.counts.value("Han") && std::max(c.hint,c.ortho)>=.33) bonus+=.16;
  c.score=std::min(1.0,(c.ml*.74+c.hint*.14+c.distinct*.12+c.ortho*.08)*(.2+.8*c.script)+bonus);
  candidates.append(c);
 }
 std::stable_sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.score>b.score;});
 QVector<Candidate> compatible;
 for(const auto &c:candidates) if(c.script>=.1) compatible.append(c);
 if(scripts.letters && !compatible.isEmpty()) candidates=compatible;
 double mass=0; const double maximum=candidates.first().score;
 for(auto &c:candidates){c.confidence=std::exp((c.score-maximum)/.07);mass+=c.confidence;}
 for(auto &c:candidates)c.confidence/=mass;
 const auto &top=candidates.first(); const Candidate second=candidates.size()>1?candidates[1]:Candidate{};
 bool reliableResult=reliable(top,second,useful,tokens);
 QString language=reliableResult?d.languages[top.index].code:QString{};
 double confidence=top.confidence, englishCandidate=0, preferredMass=0;
 for(const auto &c:candidates){const auto code=d.languages[c.index].code;if(code=="en")englishCandidate=c.confidence;if(code==target)preferredMass=c.confidence;}
 const double english=d.english(text,scripts); const bool englishLikely=english>=.58,englishStrong=english>=.74;
 QString reason=QStringLiteral("uncertain-language");
 if(scripts.dominant=="Latin" && useful>=10){
  const bool topEnglish=d.languages[top.index].code=="en";
  if((topEnglish && english>=.24 && (top.confidence-second.confidence>=.02 || englishLikely)) || (!topEnglish && englishLikely && top.confidence<.8 && (englishCandidate>=.02 || englishStrong))){language="en";confidence=std::max(confidence,std::min(.98,english*.95));reliableResult=true;reason="english-evidence-promotion";}
 }
 double foreignMass=0,familyMass=0; QHash<QString,double> familyTotals; QHash<QString,int> familyCounts; QString dominantFamily;
 for(int i=0;i<std::min(5,int(candidates.size()));++i){const auto &c=candidates[i];const auto &l=d.languages[c.index];if((i>=2 && c.confidence<.04)||l.code==target)continue;foreignMass+=c.confidence;if(!l.family.isEmpty()){familyTotals[l.family]+=c.confidence;++familyCounts[l.family];}}
 for(auto it=familyTotals.begin();it!=familyTotals.end();++it)if(it.value()>familyMass){familyMass=it.value();dominantFamily=it.key();}
 foreignMass=std::clamp(foreignMass,0.0,1.0);familyMass=std::clamp(familyMass,0.0,1.0);
 const bool coherent=familyCounts.value(dominantFamily)>=2 && foreignMass>0 && familyMass/foreignMass>=.75;
 QStringList preferredScripts;for(const auto &l:d.languages)if(l.code==target)preferredScripts=l.scripts;
 bool needed=false;double translationConfidence=0;
 if(!reliableResult || language.isEmpty()){
  const bool safe=preferredMass<=.18 && (target!="en" || !englishLikely);
  if(useful>=8 && safe && !scripts.dominant.isEmpty() && scripts.dominant!="Other" && compatibility(preferredScripts,scripts)<.2){needed=true;translationConfidence=std::max({foreignMass,top.confidence,.75});reason="foreign-script-clear-exact-language-uncertain";}
  else if(useful>=8 && safe && coherent && familyMass>=.6){needed=true;translationConfidence=familyMass;reason="related-foreign-language-cluster";}
  else translationConfidence=std::max(preferredMass,top.confidence);
 }else if(language==target){translationConfidence=std::max(preferredMass,top.confidence);reason="matches-preferred-language";}
 else if(target=="en" && (scripts.dominant=="Latin" || scripts.dominant=="Other" || scripts.dominant.isEmpty()) && englishStrong && (englishCandidate>=.08 || top.confidence<.93)){translationConfidence=std::max(english,englishCandidate);reason="english-guard";}
 else{needed=true;translationConfidence=std::max(top.confidence,foreignMass);reason="reliable-language-mismatch";}
 const bool automation=reliableResult || (needed && translationConfidence>=.50);
 const double automationConfidence=reliableResult?std::max(confidence,translationConfidence):translationConfidence;
 const double threshold=std::clamp(job.value("threshold").toDouble(.85),.50,1.0);
 result.insert("language",language.isEmpty()?d.languages[top.index].code:language);
 result.insert("confidence",automationConfidence);result.insert("automationReliable",automation);
 const int minimumWords=std::clamp(job.value("minimumWords").toInt(4),4,20);
 const double minimumMargin=std::clamp(job.value("minimumMargin").toDouble(0),0.0,1.0);
 const bool exactEnough=job.value("allowClusters").toBool(true) || reliableResult;
 const bool marginEnough=top.confidence-second.confidence>=minimumMargin;
 const bool ignored=d.ignored.contains(QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex()));
 result.insert("translate",words>=minimumWords && needed && automation && automationConfidence>=threshold && exactEnough && marginEnough && !ignored);
 result.insert("candidateMargin",top.confidence-second.confidence);
 result.insert("rawTopConfidence",top.confidence);
 result.insert("modelReliable",reliableResult);
 result.insert("reason",ignored?QStringLiteral("locally-ignored-text"):words<minimumWords?QStringLiteral("below-minimum-words"):!exactEnough?QStringLiteral("exact-language-required"):!marginEnough?QStringLiteral("candidate-margin-too-small"):reason);
 result.insert("elapsedMs",timer.nsecsElapsed()/1000000.0);
 return result;
}

QJsonObject NativeLanguageDetector::package() const
{
 return {{"format","bh-native-model"},{"formatVersion",1},{"model",data_->sourceModel},{"metadata",data_->metadata}};
}
void NativeLanguageDetector::writePackage(const QString &path, const QJsonObject &package)
{
 const auto bytes=QJsonDocument(package).toJson(QJsonDocument::Compact);
 if(bytes.size()>40*1024*1024) throw std::runtime_error("Model export exceeds 40 MiB");
 QSaveFile file(path);
 if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) throw std::runtime_error("Could not save model atomically");
}
QJsonObject NativeLanguageDetector::correctedPackage(const QString &input, const QString &label) const
{
 const auto &d=*data_;
 if(input.size()>2000 || input.trimmed().isEmpty()) throw std::runtime_error("Correction needs a sentence of at most 2000 characters");
 const QString code=label.trimmed().toLower();
 auto profiles=d.sourceModel.value("languages").toObject();
 if(!profiles.contains(code)) throw std::runtime_error("Unknown correction language code");
 const QString text=d.normalize(input,{});
 if(matches(text,rx(R"([\p{L}\p{M}])"))<10 || matches(text,rx(R"(\p{Han}|[\p{Hiragana}\p{Katakana}]|\p{L}[\p{L}\p{M}]*)"))<4) throw std::runtime_error("Use at least four useful words for a correction");
 const auto points=("_"+QString(text).replace(' ','_')+"_").toStdU32String();
 Numbers sample;double norm=0;
 for(int n=d.minN;n<=d.maxN;++n)for(size_t i=0;i+n<=points.size();++i)++sample[QString::fromUcs4(points.data()+i,n)];
 for(auto it=sample.begin();it!=sample.end();++it){it.value()*=d.idf.value(it.key(),1);norm+=it.value()*it.value();}
 norm=std::sqrt(norm);if(!norm)throw std::runtime_error("No useful correction features");
 auto profile=profiles.value(code).toObject();const auto prior=profile.value("vector").toObject();
 Numbers combined;
 for(auto it=prior.begin();it!=prior.end();++it)combined[it.key()]=it.value().toDouble()*.92;
 for(auto it=sample.begin();it!=sample.end();++it)combined[it.key()]+=.08*it.value()/norm;
 if(combined.size()>100000)throw std::runtime_error("Correction exceeds profile feature limit");
 double magnitude=0;for(double value:combined)magnitude+=value*value;magnitude=std::sqrt(magnitude);
 QJsonObject vector;for(auto it=combined.begin();it!=combined.end();++it)vector.insert(it.key(),it.value()/magnitude);
 profile.insert("vector",vector);profile.insert("samples",std::min(1000000000,profile.value("samples").toInt())+1);profiles.insert(code,profile);
 auto model=d.sourceModel;model.insert("languages",profiles);
 auto out=package();out.insert("model",model);return out;
}
QJsonObject NativeLanguageDetector::ignoredPackage(const QString &input) const
{
 if(input.size()>2000)throw std::runtime_error("Phrase is too long");
 const auto text=data_->normalize(input,{});
 if(text.isEmpty())throw std::runtime_error("No useful text to ignore");
 auto ignored=data_->ignored;
 ignored.insert(QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex()));
 if(ignored.size()>1000)throw std::runtime_error("Ignored phrase limit reached");
 QJsonArray hashes;for(const auto &hash:ignored)hashes.append(hash);
 auto meta=data_->metadata;meta.insert("ignoredTextHashes",hashes);
 auto out=package();out.insert("metadata",meta);return out;
}
