#pragma once

#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QUrl>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;
class QTimer;

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
	QPushButton *countdownStartButton_ = nullptr;
	QPushButton *countdownCancelButton_ = nullptr;
	QPushButton *countdownPreset30_ = nullptr;
	QPushButton *countdownPreset60_ = nullptr;
	QPushButton *countdownPreset120_ = nullptr;
	QPushButton *countdownPreset300_ = nullptr;
	QLineEdit *winLossColor_ = nullptr;
	QSpinBox *winLossSize_ = nullptr;
	QCheckBox *winLossGameTitle_ = nullptr;
	QCheckBox *winLossRatio_ = nullptr;
	QCheckBox *winLossWinPercent_ = nullptr;
	QCheckBox *winLossLossPercent_ = nullptr;
	QCheckBox *winLossLastGame_ = nullptr;
	QLineEdit *deathColor_ = nullptr;
	QSpinBox *deathSize_ = nullptr;
	QCheckBox *deathShowTitle_ = nullptr;
	QCheckBox *deathUseCustomText_ = nullptr;
	QLineEdit *deathCustomText_ = nullptr;
	QLineEdit *shotColor_ = nullptr;
	QSpinBox *shotSize_ = nullptr;
	QLineEdit *shotCustomText_ = nullptr;
	QComboBox *creditsTitlesMode_ = nullptr;
	QCheckBox *creditsShowAvg_ = nullptr;
	QCheckBox *creditsShowBoxArt_ = nullptr;
	QSpinBox *creditsScrollSpeed_ = nullptr;
	QLineEdit *creditsBannerText_ = nullptr;
	QComboBox *creditsBannerPosition_ = nullptr;
	QComboBox *deathCounterSelect_ = nullptr;
	QLabel *deathCounterValue_ = nullptr;
	QSpinBox *deathCounterSetValue_ = nullptr;
	QPushButton *deathCounterMinusButton_ = nullptr;
	QPushButton *deathCounterPlusButton_ = nullptr;
	QPushButton *deathCounterSetButton_ = nullptr;
	QPushButton *deathCounterResetButton_ = nullptr;
	QCheckBox *loyaltyEnabled_ = nullptr;
	QLineEdit *loyaltyCurrency_ = nullptr;
	QSpinBox *loyaltyAmount_ = nullptr;
	QSpinBox *loyaltyInterval_ = nullptr;
	QSpinBox *loyaltyTicketCost_ = nullptr;
	QSpinBox *loyaltyMaxTickets_ = nullptr;
	QSpinBox *loyaltyRafflePoints_ = nullptr;
	QSpinBox *loyaltyRaffleDuration_ = nullptr;
	QLabel *loyaltySummary_ = nullptr;
	QLabel *giveawayStatus_ = nullptr;
	QLineEdit *giveawayName_ = nullptr;
	QSpinBox *giveawayWinnerCount_ = nullptr;
	QPushButton *loyaltySaveButton_ = nullptr;
	QPushButton *giveawayStartButton_ = nullptr;
	QPushButton *giveawayPauseButton_ = nullptr;
	QPushButton *giveawayResumeButton_ = nullptr;
	QPushButton *giveawayEndButton_ = nullptr;
	QPushButton *giveawayPickButton_ = nullptr;
	QPushButton *raffleStartButton_ = nullptr;
	QPushButton *raffleCancelButton_ = nullptr;
	QComboBox *walkOnSelect_ = nullptr;
	QLabel *walkOnSummary_ = nullptr;
	QCheckBox *walkOnEnabled_ = nullptr;
	QCheckBox *walkOnOverlay_ = nullptr;
	QCheckBox *walkOnShoutout_ = nullptr;
	QCheckBox *walkOnAnnouncement_ = nullptr;
	QComboBox *walkOnColor_ = nullptr;
	QPushButton *walkOnSaveButton_ = nullptr;
	QPushButton *walkOnTestButton_ = nullptr;
	QTimer *refreshTimer_ = nullptr;
	QTimer *countdownTickTimer_ = nullptr;
	QMap<QString, QCheckBox *> channelToggles_;
	QMap<QString, QCheckBox *> eventToggles_;
	QMap<QString, QUrl> dashboardLinks_;
	QMap<QString, QUrl> browserSourceUrls_;
	QMap<QString, QJsonObject> overlaySettings_;
	QMap<QString, QJsonObject> deathCounters_;
	QMap<QString, QJsonObject> walkOns_;
	QJsonObject loyaltyState_;
	QComboBox *testEventType_ = nullptr;
	QPushButton *testEventButton_ = nullptr;
	QLabel *sourceStatus_ = nullptr;
	QMap<QString, QPushButton *> sourceInstallButtons_;
	QString token_;
	QString credentialStorageWarning_;
	qint64 countdownEndTime_ = 0;
	bool applyingState_ = false;
	bool botIsModerator_ = false;

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
	void saveOverlaySettings(const QString &key);
	void testStreamEvent();
	void installBrowserSource(const QString &key);
	void syncExistingBrowserSources();
	void updateDeathCounterSelection();
	void runDeathCounterAction(const QString &action, int value = -1);
	void applyLoyaltyState(const QJsonObject &loyalty);
	void saveLoyaltySettings();
	void setLoyaltyActive(bool enabled);
	void runGiveawayAction(const QString &action);
	void updateWalkOnSelection();
	void saveWalkOnSettings();
	void testWalkOn();
	void updateCountdownStatus();
	void startCountdown();
	void startCountdownPreset(int seconds);
	void cancelCountdown();
	void openDashboardLink(const QString &key);
};
