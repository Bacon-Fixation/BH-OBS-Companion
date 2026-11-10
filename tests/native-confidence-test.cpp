#include "native-language-detector.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

int main(int argc, char **argv)
{
 QCoreApplication app(argc, argv);
 if (argc != 2) return 2;
 int checks = 0;
 auto check = [&](bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); };
 try {
  NativeLanguageDetector detector(QString::fromLocal8Bit(argv[1]));
  auto detect = [&](const QString &text) { return detector.detect({{"id","confidence"},{"text",text},{"target","en"}}); };
  auto hint = [&](const QString &text, const QString &language) {
   const auto result = detect(text);
   for (const auto &value : result.value("candidates").toArray()) {
    const auto candidate = value.toObject();
    if (candidate.value("language").toString() == language) return candidate.value("wordHintScore").toDouble();
   }
   return 0.0;
  };
  for (const auto &text : QStringList{"hola amigos como estan todos hoy", "bonjour merci beaucoup pour votre aide", "please keep the backup service running"}) {
   QStringList quoted;
   for (const auto &word : text.split(' ')) quoted.append(QStringLiteral("‘") + word + QStringLiteral("’"));
   const auto plain = detect(text), decorated = detect(quoted.join(' '));
   check(plain.value("language") == decorated.value("language"), "quotes changed predicted language");
   check(std::abs(plain.value("rawTopConfidence").toDouble()-decorated.value("rawTopConfidence").toDouble()) < 1e-9, "quotes changed statistical/lexical evidence");
   check(plain.value("translate") == decorated.value("translate"), "quotes changed translation decision");
  }
  check(hint("bonjour", "fr") == hint("‘bonjour’", "fr"), "quote boundary lost word hint");
  check(hint("prefixbonjoursuffix", "fr") == 0, "substring counted as whole word");
  check(hint("c'est", "fr") == hint("c’est", "fr"), "curly apostrophe changed elision");
  check(hint("c'est", "fr") == hint("est est", "fr"), "French elision lost attached word");
  check(hint("l'una", "it") == hint("una una", "it"), "Italian elision lost attached word");
  check(std::abs(hint("donde dónde donde", "es")-1.0/3) < 1e-9, "accent variants counted multiple times");
  check(hint("está", "fr") == 0, "supplied accent became another language word");
  check(std::abs(hint("está", "es")-1.0/3) < 1e-9, "Spanish accent evidence lost");
  check(hint("है है है", "hi") > hint("ह ह ह ह", "hi"), "Indic marks lost lexical distinction");
  for (const auto &text : QStringList{"gg", "https://example.com", "please keep the backup service running", "that's actually pretty funny lol"}) {
   check(!detect(text).value("translate").toBool(), "noise or English triggered translation");
  }
  const auto foreign = detect("hola amigos como estan todos hoy");
  check(foreign.value("translate").toBool(), "clear Spanish failed translation threshold");
  check(foreign.value("confidence") == foreign.value("translationConfidence"), "API confidence compatibility changed");
  check(foreign.value("languageConfidence").toDouble() <= foreign.value("translationConfidence").toDouble(), "language and translation scores inconsistent");
  auto strict = QJsonObject{{"id","strict"},{"text","hola amigos como estan todos hoy"},{"target","en"},{"threshold",1.0}};
  check(!detector.detect(strict).value("translate").toBool(), "strict threshold bypassed");
  strict.insert("threshold",.85);strict.insert("suppressedLanguages",QJsonArray{"es"});
  check(!detector.detect(strict).value("translate").toBool(), "suppression bypassed");
  QTextStream(stdout) << "Native confidence: " << checks << " checks passed\n";
  return 0;
 } catch (const std::exception &error) {
  QTextStream(stderr) << "After " << checks << " checks: " << error.what() << "\n";
  return 1;
 }
}
