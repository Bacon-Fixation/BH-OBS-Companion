#pragma once
#include <QWidget>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <functional>
class QCheckBox;
class QLineEdit;
class QLabel;
class QSpinBox;
class QPushButton;
class QTreeWidget;
class QThread;
class NativeLanguageWorker;
class QTimer;
class QNetworkReply;

class LanguagePanel final : public QWidget {
public:
 using Sender = std::function<QNetworkReply *(const QString &, const QJsonObject &)>;
 LanguagePanel(Sender send, std::function<QString()> session, const QString &settingsPath, QWidget *parent = nullptr);
 ~LanguagePanel() override;
private:
 Sender send_;
 std::function<QString()> session_;
 QString settingsPath_;
 QCheckBox *enabled_, *automatic_;
 QLineEdit *sample_, *target_;
 QSpinBox *threshold_, *minimumWords_, *margin_;
 QCheckBox *clusters_;
 QLineEdit *correctLanguage_;
 QLabel *modelStatus_;
 QPushButton *applyCorrection_;
 QList<QPushButton *> modelButtons_;
 bool modelBusy_ = false;
 QString modelStorage_;
 void modelAction(const QString &operation, QJsonObject request = {});
 QPushButton *test_;
 QLabel *status_;
 QTreeWidget *history_;
 QThread *workerThread_;
 NativeLanguageWorker *worker_;
 QTimer *pollTimer_, *deadline_;
 QJsonObject pending_;
 QString pendingSession_;
 bool ready_ = false, pollBusy_ = false, resultBusy_ = false;
 int generation_ = 0;
 void start();
 void stop();
 void poll();
 void dispatch(QJsonObject job, const QString &session);
 void handleResult(const QJsonObject &result);
 void showResult(const QJsonObject &result, const QString &action);
 void savePreferences();
};
