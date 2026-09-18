#pragma once

#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QUrl>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;

class BaconsHelperDock final : public QWidget {
	Q_OBJECT

public:
	explicit BaconsHelperDock(QWidget *parent = nullptr);
	~BaconsHelperDock() override = default;

private:
	QNetworkAccessManager *network_ = nullptr;
	QLineEdit *serverUrl_ = nullptr;
	QLineEdit *pairCode_ = nullptr;
	QPushButton *pairButton_ = nullptr;
	QPushButton *disconnectButton_ = nullptr;
	QPushButton *refreshButton_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *channel_ = nullptr;
	QSpinBox *countdownSeconds_ = nullptr;
	QLabel *countdownStatus_ = nullptr;
	QMap<QString, QCheckBox *> channelToggles_;
	QMap<QString, QCheckBox *> eventToggles_;
	QMap<QString, QUrl> dashboardLinks_;
	QString token_;
	QString credentialStorageWarning_;
	bool applyingState_ = false;

	void buildUi();
	void loadLocalSettings();
	bool saveLocalSettings(QString *credentialError = nullptr);
	void setStatus(const QString &message, bool connected);
	void setControlsEnabled(bool enabled);
	QUrl endpoint(const QString &path) const;
	QNetworkReply *sendJson(const QByteArray &method, const QString &path,
						 const QJsonObject &payload = {});
	bool validateServerUrl();
	void handleAuthFailure(QNetworkReply *reply);
	void pair();
	void disconnectAccount();
	void refresh();
	void applySettingsPayload(const QJsonObject &payload);
	void saveChannelToggle(const QString &key, bool checked);
	void saveStreamEvent(const QString &key, bool checked);
	void startCountdown();
	void cancelCountdown();
	void openDashboardLink(const QString &key);
};
