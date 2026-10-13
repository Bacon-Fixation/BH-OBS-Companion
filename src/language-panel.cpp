#include "language-panel.hpp"
#include <obs-module.h>
#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QThread>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QGroupBox>
#include "native-language-worker.hpp"
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QUuid>
#include <utility>

LanguagePanel::LanguagePanel(Sender send, std::function<QString()> session, const QString &settingsPath, QWidget *parent)
 : QWidget(parent), send_(std::move(send)), session_(std::move(session)), settingsPath_(settingsPath)
{
 auto *layout = new QVBoxLayout(this);
 auto *scroll = new QScrollArea(); scroll->setWidgetResizable(true);
 auto *content = new QWidget(); auto *form = new QFormLayout(content);
 form->setRowWrapPolicy(QFormLayout::WrapLongRows);
 scroll->setWidget(content); layout->addWidget(scroll);
 auto *note = new QLabel(tr("(Experimental Expect Bugs) Have language detection run locally in OBS and allow you to fine tune detection to better suite your individual needs instead of the generic global settings."));
 note->setWordWrap(true); note->setTextFormat(Qt::PlainText); form->addRow(note);
 enabled_ = new QCheckBox(tr("Run local detector (preview by default)"));
 automatic_ = new QCheckBox(tr("Automatically send translation in chat"));
 threshold_ = new QSpinBox(); threshold_->setRange(50, 100); threshold_->setSuffix(tr("% minimum score"));
 form->addRow(enabled_); form->addRow(automatic_); form->addRow(tr("Confidence"), threshold_);
 minimumWords_=new QSpinBox();minimumWords_->setRange(4,20);
 margin_=new QSpinBox();margin_->setRange(0,100);margin_->setSuffix(tr("% minimum candidate gap"));
 clusters_=new QCheckBox(tr("Allow related-language groups when exact language is uncertain"));
 form->addRow(tr("Minimum useful words"),minimumWords_);form->addRow(tr("Ambiguity guard"),margin_);form->addRow(clusters_);
 auto *cautious=new QPushButton(tr("Use cautious settings (fewer triggers)"));form->addRow(cautious);
 connect(cautious,&QPushButton::clicked,this,[this]{threshold_->setValue(95);minimumWords_->setValue(6);margin_->setValue(15);clusters_->setChecked(false);savePreferences();});
 auto *scoreNote=new QLabel(tr("Scores are model evidence, not calibrated probabilities. Stricter settings skip more messages; corrections can improve or worsen predictions."));scoreNote->setWordWrap(true);form->addRow(scoreNote);

 status_ = new QLabel(tr("Stopped. Automatic translation is off.")); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); form->addRow(status_);
 sample_ = new QLineEdit(); sample_->setMaxLength(2000); sample_->setPlaceholderText(tr("Enter a sentence to test offline"));
 target_ = new QLineEdit(QStringLiteral("en")); target_->setMaxLength(12);
 test_ = new QPushButton(tr("Test text (never posts to chat)"));
 form->addRow(tr("Test text"), sample_); form->addRow(tr("Test target language"), target_); form->addRow(test_);
 auto *modelBox=new QGroupBox(tr("Model and corrections"));auto *modelForm=new QFormLayout(modelBox);modelForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
 modelStatus_=new QLabel(tr("Start the detector to import, export or fine-tune. Select a history row to review its text."));modelStatus_->setWordWrap(true);modelStatus_->setTextFormat(Qt::PlainText);modelForm->addRow(modelStatus_);
 correctLanguage_=new QLineEdit(QStringLiteral("en"));correctLanguage_->setMaxLength(32);correctLanguage_->setPlaceholderText(tr("Correct language code, e.g. en or es"));modelForm->addRow(tr("Correct language"),correctLanguage_);
 auto addAction=[this,modelForm](const QString &label,const QString &operation){auto *button=new QPushButton(label);modelButtons_.append(button);modelForm->addRow(button);connect(button,&QPushButton::clicked,this,[this,operation]{modelAction(operation);});return button;};
 addAction(tr("Preview correction using test text"),QStringLiteral("preview"));
 applyCorrection_=addAction(tr("Apply previewed correction"),QStringLiteral("apply"));applyCorrection_->setEnabled(false);
 addAction(tr("Ignore this normalized text"),QStringLiteral("ignore"));
 addAction(tr("Import model…"),QStringLiteral("import"));addAction(tr("Export active model…"),QStringLiteral("export"));
 addAction(tr("Undo last model change"),QStringLiteral("undo"));addAction(tr("Restore bundled model"),QStringLiteral("reset"));
 auto *privacy=new QLabel(tr("Applied corrections save reviewed messages locally (up to 200). Export includes trained weights and ignored-text hashes, but no raw review messages. Import/reset clears reviews; undo retains one previous local model."));privacy->setWordWrap(true);modelForm->addRow(privacy);
 form->addRow(modelBox);

 history_ = new QTreeWidget(); history_->setColumnCount(5);
 history_->setHeaderLabels({tr("Time"), tr("Language"), tr("Confidence"), tr("Decision"), tr("Text")});
 history_->setRootIsDecorated(false); history_->setAlternatingRowColors(true);
 history_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
 history_->header()->setStretchLastSection(true); layout->addWidget(history_, 1);
 auto *clear = new QPushButton(tr("Clear local history")); layout->addWidget(clear);
 connect(clear, &QPushButton::clicked, history_, &QTreeWidget::clear);
 modelStorage_=QDir(QFileInfo(settingsPath_).absolutePath()).filePath(QStringLiteral("language-model-custom.json"));
 QSettings settings(settingsPath_, QSettings::IniFormat);
 minimumWords_->setValue(settings.value(QStringLiteral("language/minimumWords"),4).toInt());
 margin_->setValue(settings.value(QStringLiteral("language/margin"),0).toInt());
 clusters_->setChecked(settings.value(QStringLiteral("language/clusters"),true).toBool());
 settings.remove(QStringLiteral("language/node"));
 threshold_->setValue(settings.value(QStringLiteral("language/threshold"), 85).toInt());
 // Posting and live polling require an explicit opt-in each OBS session.
 automatic_->setEnabled(false); test_->setEnabled(false);
 workerThread_ = new QThread(this);
 worker_ = new NativeLanguageWorker();
 worker_->moveToThread(workerThread_);
 connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
 workerThread_->start();
 pollTimer_ = new QTimer(this); pollTimer_->setInterval(2000);
 deadline_ = new QTimer(this); deadline_->setSingleShot(true); deadline_->setInterval(10000);
 connect(pollTimer_, &QTimer::timeout, this, [this] { poll(); });
 connect(deadline_, &QTimer::timeout, this, [this] { enabled_->setChecked(false); status_->setText(tr("Detector timed out. Disable and restart local detection.")); });
 connect(enabled_, &QCheckBox::toggled, this, [this](bool on) { if (on) start(); else stop(); });
 connect(threshold_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { savePreferences(); });
 connect(minimumWords_,qOverload<int>(&QSpinBox::valueChanged),this,[this]{savePreferences();});
 connect(margin_,qOverload<int>(&QSpinBox::valueChanged),this,[this]{savePreferences();});
 connect(clusters_,&QCheckBox::toggled,this,[this]{savePreferences();});
 for(auto *field:{sample_,target_,correctLanguage_})connect(field,&QLineEdit::textChanged,this,[this]{applyCorrection_->setEnabled(false);});
 connect(history_,&QTreeWidget::itemSelectionChanged,this,[this]{auto *item=history_->currentItem();if(!item || modelBusy_)return;const auto result=item->data(0,Qt::UserRole).toJsonObject();sample_->setText(result.value(QStringLiteral("text")).toString());target_->setText(result.value(QStringLiteral("target")).toString(QStringLiteral("en")));correctLanguage_->setText(result.value(QStringLiteral("language")).toString(QStringLiteral("en")));});
 connect(worker_,&NativeLanguageWorker::managed,this,[this](const QJsonObject &report,int generation){
  modelBusy_=false;for(auto *field:{sample_,target_,correctLanguage_})field->setEnabled(true);enabled_->setEnabled(true);for(auto *button:modelButtons_)button->setEnabled(true);applyCorrection_->setEnabled(false);
  if(generation!=generation_)return;
  modelStatus_->setText(report.value(QStringLiteral("message")).toString());
  applyCorrection_->setEnabled(report.value(QStringLiteral("canApply")).toBool());
  ready_=report.value(QStringLiteral("modelReady")).toBool() && enabled_->isChecked();
  automatic_->setEnabled(ready_);test_->setEnabled(ready_);if(ready_)pollTimer_->start();
 });

 connect(worker_, &NativeLanguageWorker::ready, this, [this](int generation) {
  if (generation != generation_ || !enabled_->isChecked()) return;
  ready_ = true; deadline_->stop(); automatic_->setEnabled(true); test_->setEnabled(true); pollTimer_->start();
  status_->setText(tr("Native detector ready. Preview mode; requests go to Bacons Helper only."));
 });
 connect(worker_, &NativeLanguageWorker::result, this, [this](const QJsonObject &result) { handleResult(result); });
 connect(worker_, &NativeLanguageWorker::failed, this, [this](const QString &message, int generation) {
  if (generation != generation_) return;
  enabled_->setChecked(false); status_->setText(tr("Native detector unavailable: %1").arg(message));
 });
 connect(test_, &QPushButton::clicked, this, [this] {
  if (!sample_->text().trimmed().isEmpty()) dispatch({{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)}, {QStringLiteral("text"), sample_->text()}, {QStringLiteral("target"), target_->text().trimmed()}}, QString{});
 });
}
LanguagePanel::~LanguagePanel()
{
 // No detached work or forced thread termination. Only bounded local CPU work
 // runs here, and all queued UI deliveries use QObject receiver lifetime checks.
 workerThread_->quit();
 workerThread_->wait();
}
void LanguagePanel::savePreferences()
{
 QSettings settings(settingsPath_, QSettings::IniFormat);
 settings.setValue(QStringLiteral("language/threshold"), threshold_->value());
 settings.setValue(QStringLiteral("language/minimumWords"),minimumWords_->value());
 settings.setValue(QStringLiteral("language/margin"),margin_->value());
 settings.setValue(QStringLiteral("language/clusters"),clusters_->isChecked());
}
void LanguagePanel::start()
{
 char *path = obs_module_file("language-guesser/model.json");
 if (!path) { enabled_->setChecked(false); status_->setText(tr("Language model is missing. Reinstall the complete plugin data.")); return; }
 const QString directory = QFileInfo(QString::fromUtf8(path)).absolutePath(); bfree(path);
 const int generation = ++generation_; ready_ = false; savePreferences();
 status_->setText(tr("Loading native language model…")); deadline_->start();
 auto *worker = worker_;
 const QString storage=modelStorage_;
 QMetaObject::invokeMethod(worker, [worker, directory, generation, storage] { worker->load(directory, generation, storage); }, Qt::QueuedConnection);
}
void LanguagePanel::stop()
{
 ++generation_; pollTimer_->stop(); deadline_->stop(); ready_ = false;
 pending_ = {}; pendingSession_.clear();
 automatic_->setChecked(false); automatic_->setEnabled(false); test_->setEnabled(false);
 status_->setText(tr("Stopped. Automatic translation is off."));
}
void LanguagePanel::poll()
{
 const QString session = session_();
 if (!ready_ || !enabled_->isChecked() || session.isEmpty() || pollBusy_ || resultBusy_ || !pending_.isEmpty()) return;
 pollBusy_ = true; const int generation = generation_;
 auto *reply = send_(QStringLiteral("/api/obs-plugin/language/poll"), {});
 connect(reply, &QNetworkReply::finished, this, [this, reply, session, generation] {
  pollBusy_ = false;
  const auto body = QJsonDocument::fromJson(reply->readAll()).object();
  const bool ok = reply->error() == QNetworkReply::NoError && body.value(QStringLiteral("ok")).toBool();
  reply->deleteLater();
  if (generation != generation_ || session != session_() || !enabled_->isChecked()) return;
  if (!ok) { status_->setText(tr("Chat polling failed. Check pairing and update Bacons Helper; retrying.")); return; }
  if (!body.value(QStringLiteral("enabled")).toBool()) { status_->setText(tr("Enable Translator in the Channel tab to process live chat.")); return; }
  const auto jobs = body.value(QStringLiteral("jobs")).toArray();
  if (!jobs.isEmpty()) dispatch(jobs.first().toObject(), session);
  else status_->setText(automatic_->isChecked() ? tr("Listening - automatic translation enabled.") : tr("Listening - preview only; no chat messages will be posted."));
 });
}
void LanguagePanel::dispatch(QJsonObject job, const QString &session)
{
 if (!ready_ || !pending_.isEmpty() || !enabled_->isChecked()) return;
 if (!job.value(QStringLiteral("id")).isString() || !job.value(QStringLiteral("text")).isString()) return;
 job.insert(QStringLiteral("threshold"), threshold_->value() / 100.0);
 job.insert(QStringLiteral("minimumWords"),minimumWords_->value());
 job.insert(QStringLiteral("minimumMargin"),margin_->value()/100.0);
 job.insert(QStringLiteral("allowClusters"),clusters_->isChecked());
 pending_ = job; pendingSession_ = session; test_->setEnabled(false);
 job.insert(QStringLiteral("_generation"), generation_);
 auto *worker = worker_;
 QMetaObject::invokeMethod(worker, [worker, job] { worker->detect(job); }, Qt::QueuedConnection);
 deadline_->start();
}
void LanguagePanel::handleResult(const QJsonObject &result)
{
 if (result.value(QStringLiteral("_generation")).toInt() != generation_ || !enabled_->isChecked() || pending_.isEmpty()) return;
 if (result.value(QStringLiteral("id")) != pending_.value(QStringLiteral("id"))) {
  enabled_->setChecked(false); status_->setText(tr("Invalid detector response. Restart local detection.")); return;
 }
  deadline_->stop(); test_->setEnabled(true);
  const QString session = pendingSession_; pending_ = {}; pendingSession_.clear();
  const bool live = !session.isEmpty() && session == session_();
  const bool translate = live && automatic_->isChecked() && result.value(QStringLiteral("translate")).toBool();
  if (!session.isEmpty() && !live) return;
  showResult(result, translate ? tr("Requesting translation") : result.value(QStringLiteral("translate")).toBool() ? tr("Would translate (preview)") : result.value(QStringLiteral("reason")).toString());
  if (!live) return;
  QJsonObject payload{{QStringLiteral("id"), result.value(QStringLiteral("id"))}, {QStringLiteral("translate"), translate},
   {QStringLiteral("language"), result.value(QStringLiteral("language"))}, {QStringLiteral("confidence"), result.value(QStringLiteral("confidence"))}, {QStringLiteral("automationReliable"), result.value(QStringLiteral("automationReliable"))}};
  resultBusy_ = true; const int generation = generation_;
  auto *reply = send_(QStringLiteral("/api/obs-plugin/language/result"), payload);
  connect(reply, &QNetworkReply::finished, this, [this, reply, result, session, generation] {
   resultBusy_ = false;
   const auto body = QJsonDocument::fromJson(reply->readAll()).object();
   const bool ok = reply->error() == QNetworkReply::NoError && body.value(QStringLiteral("ok")).toBool();
   reply->deleteLater();
   if (generation != generation_ || session != session_()) return;
   const QString outcome = ok ? body.value(QStringLiteral("status")).toString() : tr("Request failed; not retried");
   showResult(result, outcome); status_->setText(outcome);
  });
}
void LanguagePanel::showResult(const QJsonObject &result, const QString &action)
{
 auto *item = new QTreeWidgetItem(QStringList{QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
  result.value(QStringLiteral("language")).toString(), QString::number(result.value(QStringLiteral("confidence")).toDouble() * 100, 'f', 1) + '%', action, result.value(QStringLiteral("text")).toString()});
 item->setData(0,Qt::UserRole,result);
 item->setToolTip(3, action); item->setToolTip(4, result.value(QStringLiteral("text")).toString());
 history_->insertTopLevelItem(0, item);
 while (history_->topLevelItemCount() > 100) delete history_->takeTopLevelItem(history_->topLevelItemCount() - 1);
}

void LanguagePanel::modelAction(const QString &operation,QJsonObject request)
{
 if(modelBusy_)return;
 if(operation==QStringLiteral("import") || operation==QStringLiteral("export")){
  const QString path=operation==QStringLiteral("import")?QFileDialog::getOpenFileName(this,tr("Import Bacons Helper native model"),{},tr("Native model (*.json)")):QFileDialog::getSaveFileName(this,tr("Export active native model"),QStringLiteral("bacons-helper-model.json"),tr("Native model (*.json)"));
  if(path.isEmpty())return;request.insert(QStringLiteral("path"),path);
 }
 request.insert(QStringLiteral("text"),sample_->text());request.insert(QStringLiteral("target"),target_->text());request.insert(QStringLiteral("language"),correctLanguage_->text());
 // Invalidate in-flight detections before changing the model. Model operations
 // execute serially on the same worker; automatic posting must be re-enabled.
 automatic_->setChecked(false);automatic_->setEnabled(false);ready_=false;
 pollTimer_->stop();deadline_->stop();pending_={};pendingSession_.clear();
 modelBusy_=true;for(auto *field:{sample_,target_,correctLanguage_})field->setEnabled(false);enabled_->setEnabled(false);test_->setEnabled(false);for(auto *button:modelButtons_)button->setEnabled(false);
 modelStatus_->setText(tr("Processing model operation locally…"));
 const int generation=++generation_;auto *worker=worker_;
 QMetaObject::invokeMethod(worker,[worker,operation,request,generation]{worker->manage(operation,request,generation);},Qt::QueuedConnection);
}
