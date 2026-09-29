#ifdef BH_HAVE_LIBSECRET
// libsecret pulls in GLib/GDBus declarations with a member named `signals`.
// Include it before Qt so Qt keyword macros can never rewrite GLib headers.
#include <libsecret/secret.h>
#endif

#include "bacons-helper-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSslSocket>
#include <QStringList>
#include <QSysInfo>
#include <QTabWidget>
#include <QTimer>
#include <QtNumeric>
#include <QUrl>
#include <QVBoxLayout>

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
constexpr auto kDefaultServer = "https://baconshelper.com";

QString configPath()
{
	char *path = obs_module_config_path("bacons-helper.ini");
	if (!path)
		return {};
	const QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString protectToken(const QString &token)
{
	if (token.isEmpty())
		return {};
#ifdef _WIN32
	const QByteArray input = token.toUtf8();
	DATA_BLOB source{};
	source.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()));
	source.cbData = static_cast<DWORD>(input.size());
	DATA_BLOB encrypted{};
	if (CryptProtectData(&source, L"Bacons Helper OBS credential", nullptr, nullptr,
					 nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
		const QByteArray bytes(reinterpret_cast<char *>(encrypted.pbData),
						  static_cast<int>(encrypted.cbData));
		LocalFree(encrypted.pbData);
		return QStringLiteral("dpapi:") + QString::fromLatin1(bytes.toBase64());
	}
#endif
	return {};
}

QString unprotectToken(const QString &stored)
{
	if (stored.startsWith(QStringLiteral("dpapi:"))) {
#ifdef _WIN32
		const QByteArray encrypted = QByteArray::fromBase64(stored.mid(6).toLatin1());
		DATA_BLOB source{};
		source.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
		source.cbData = static_cast<DWORD>(encrypted.size());
		DATA_BLOB clear{};
		if (CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
						 CRYPTPROTECT_UI_FORBIDDEN, &clear)) {
			const QString token = QString::fromUtf8(reinterpret_cast<char *>(clear.pbData),
										 static_cast<int>(clear.cbData));
			LocalFree(clear.pbData);
			return token;
		}
#endif
		return {};
	}
	return {};
}

#ifdef BH_HAVE_LIBSECRET
const SecretSchema kBaconsHelperSecretSchema = {
	"com.baconshelper.obs.credential",
	SECRET_SCHEMA_NONE,
	{
		{"application", SECRET_SCHEMA_ATTRIBUTE_STRING},
		{"server", SECRET_SCHEMA_ATTRIBUTE_STRING},
		{nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
	},
};

QString secretServiceLookup(const QString &server, QString *errorMessage)
{
	const QByteArray serverUtf8 = server.toUtf8();
	GError *error = nullptr;
	gchar *password = secret_password_lookup_sync(
		&kBaconsHelperSecretSchema, nullptr, &error,
		"application", "bacons-helper-obs",
		"server", serverUtf8.constData(),
		nullptr);

	if (error) {
		if (errorMessage)
			*errorMessage = QString::fromUtf8(error->message);
		g_error_free(error);
		return {};
	}
	if (!password)
		return {};

	const QString token = QString::fromUtf8(password);
	secret_password_free(password);
	return token;
}

bool secretServiceStore(const QString &server, const QString &token, QString *errorMessage)
{
	const QByteArray serverUtf8 = server.toUtf8();
	const QByteArray tokenUtf8 = token.toUtf8();
	GError *error = nullptr;
	const gboolean ok = secret_password_store_sync(
		&kBaconsHelperSecretSchema,
		SECRET_COLLECTION_DEFAULT,
		"Bacons Helper OBS credential",
		tokenUtf8.constData(),
		nullptr,
		&error,
		"application", "bacons-helper-obs",
		"server", serverUtf8.constData(),
		nullptr);

	if (!ok || error) {
		if (errorMessage)
			*errorMessage = error ? QString::fromUtf8(error->message)
								  : QStringLiteral("Secret Service rejected the credential.");
		if (error)
			g_error_free(error);
		return false;
	}
	return true;
}

bool secretServiceClear(const QString &server, QString *errorMessage)
{
	const QByteArray serverUtf8 = server.toUtf8();
	GError *error = nullptr;
	const gboolean ok = secret_password_clear_sync(
		&kBaconsHelperSecretSchema,
		nullptr,
		&error,
		"application", "bacons-helper-obs",
		"server", serverUtf8.constData(),
		nullptr);

	// A false return without GError only means that no matching secret was
	// present. That is already the desired state for a clear operation.
	if (error) {
		if (errorMessage)
			*errorMessage = QString::fromUtf8(error->message);
		g_error_free(error);
		return false;
	}
	Q_UNUSED(ok);
	return true;
}
#endif

QCheckBox *addToggle(QVBoxLayout *layout, const QString &label, const QString &key,
					 QMap<QString, QCheckBox *> &target)
{
	auto *toggle = new QCheckBox(label);
	toggle->setProperty("bhKey", key);
	layout->addWidget(toggle);
	target.insert(key, toggle);
	return toggle;
}
} // namespace

BaconsHelperDock::BaconsHelperDock(QWidget *parent) : QWidget(parent)
{
#ifdef _WIN32
	// Qt 6 implements TLS through runtime plugins. OBS already owns the Qt
	// runtime. Add this module's private Qt plugin tree before probing TLS so we
	// do not have to install or overwrite plugins in OBS's global Qt directory.
	char *qtPluginPath = obs_module_file("qt-plugins");
	if (qtPluginPath) {
		const QString pluginPath = QString::fromUtf8(qtPluginPath);
		QCoreApplication::addLibraryPath(pluginPath);
		blog(LOG_INFO, "[Bacons Helper] Added private Qt plugin path: %s", qtPluginPath);
		bfree(qtPluginPath);
	}

	// Prefer the native Windows Schannel plugin packaged alongside the
	// companion instead of depending on a separately installed OpenSSL.
	const auto tlsBackends = QSslSocket::availableBackends();
	const QString tlsBackendSummary = QStringList(tlsBackends).join(QStringLiteral(", "));
	blog(LOG_INFO, "[Bacons Helper] Qt TLS backends: %s",
		 tlsBackendSummary.isEmpty() ? "none" : tlsBackendSummary.toUtf8().constData());
	if (tlsBackends.contains(QStringLiteral("schannel"))) {
		if (QSslSocket::setActiveBackend(QStringLiteral("schannel"))) {
			blog(LOG_INFO, "[Bacons Helper] Using Qt Schannel TLS backend");
		} else {
			blog(LOG_WARNING, "[Bacons Helper] Qt reported Schannel but could not activate it");
		}
	}
#endif

	network_ = new QNetworkAccessManager(this);
	buildUi();
	loadLocalSettings();
	setControlsEnabled(!token_.isEmpty());
	if (!token_.isEmpty()) {
		refresh();
	} else if (!credentialStorageWarning_.isEmpty()) {
		setStatus(QStringLiteral("⚠ Not paired — secure credential storage is unavailable: %1")
				  .arg(credentialStorageWarning_), false);
	} else {
		setStatus(QStringLiteral("✕ Not paired — generate a code in the Bacons Helper dashboard."), false);
	}
}

void BaconsHelperDock::buildUi()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);

	status_ = new QLabel();
	status_->setWordWrap(true);
	channel_ = new QLabel(QStringLiteral("Channel: not paired"));
	outer->addWidget(status_);
	outer->addWidget(channel_);

	auto *tabs = new QTabWidget();
	outer->addWidget(tabs, 1);

	auto *connection = new QWidget();
	auto *connectionLayout = new QFormLayout(connection);
	serverUrl_ = new QLineEdit(QString::fromLatin1(kDefaultServer));
	serverUrl_->setPlaceholderText(QString::fromLatin1(kDefaultServer));
	pairCode_ = new QLineEdit();
	pairCode_->setPlaceholderText(QStringLiteral("ABCD-EFGH"));
	pairCode_->setMaxLength(12);
	connectionLayout->addRow(QStringLiteral("Server"), serverUrl_);
	connectionLayout->addRow(QStringLiteral("Pairing code"), pairCode_);
	auto *connectionButtons = new QWidget();
	auto *connectionButtonsLayout = new QHBoxLayout(connectionButtons);
	connectionButtonsLayout->setContentsMargins(0, 0, 0, 0);
	pairButton_ = new QPushButton(QStringLiteral("Pair"));
	disconnectButton_ = new QPushButton(QStringLiteral("Disconnect & revoke"));
	refreshButton_ = new QPushButton(QStringLiteral("Refresh"));
	connectionButtonsLayout->addWidget(pairButton_);
	connectionButtonsLayout->addWidget(refreshButton_);
	connectionButtonsLayout->addWidget(disconnectButton_);
	connectionLayout->addRow(connectionButtons);
	auto *securityNote = new QLabel(QStringLiteral(
		"OBS receives a revocable Bacons Helper credential scoped to this channel. "
		"It never stores your Twitch OAuth token or Bacons Helper website session."));
	securityNote->setWordWrap(true);
	connectionLayout->addRow(securityNote);
	tabs->addTab(connection, QStringLiteral("Connection"));

	auto *channelTab = new QWidget();
	auto *channelLayout = new QVBoxLayout(channelTab);
	addToggle(channelLayout, QStringLiteral("Translator"), QStringLiteral("translator"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Follow-bot filter"), QStringLiteral("auto_bot_silencer"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Follow-bot removal notifications"), QStringLiteral("bot_removal_notifications"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Chat seeding"), QStringLiteral("chat_seeding"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Animal / spawnable events"), QStringLiteral("spawnables"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Bot muted"), QStringLiteral("bot_muted"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Chat notifications"), QStringLiteral("chat_notifications"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Automatic raid shoutout"), QStringLiteral("raid_shoutout"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Reduced messaging"), QStringLiteral("reduced_messaging"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Lurk command"), QStringLiteral("lurk_command"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Unlurk command"), QStringLiteral("unlurk_command"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Welcome messages"), QStringLiteral("welcome"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Walk-On messages"), QStringLiteral("walk_on_welcome"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Follower notifications"), QStringLiteral("follower_notifications"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Song notifications"), QStringLiteral("song_notification"), channelToggles_);
	channelLayout->addStretch();
	tabs->addTab(channelTab, QStringLiteral("Channel"));

	auto *eventsTab = new QWidget();
	auto *eventsLayout = new QVBoxLayout(eventsTab);
	const QList<QPair<QString, QString>> events = {
		{QStringLiteral("follow"), QStringLiteral("Follows")},
		{QStringLiteral("subscription"), QStringLiteral("Subscriptions & resubs")},
		{QStringLiteral("gift_subscription"), QStringLiteral("Gift subscriptions")},
		{QStringLiteral("raid"), QStringLiteral("Raids")},
		{QStringLiteral("cheer"), QStringLiteral("Cheers & Bits")},
		{QStringLiteral("redemption"), QStringLiteral("Channel Point redemptions")},
		{QStringLiteral("donation"), QStringLiteral("Charity donations")},
		{QStringLiteral("shoutout"), QStringLiteral("Received shoutouts")},
		{QStringLiteral("auto_live"), QStringLiteral("Auto Live")},
		{QStringLiteral("walk_on"), QStringLiteral("Walk-On Messages")},
		{QStringLiteral("follow_bot_ad_removed"), QStringLiteral("Removed follow-bot ads")},
		{QStringLiteral("custom_mini_game_winner"), QStringLiteral("Custom Mini-Game winner")},
		{QStringLiteral("giveaway_winner"), QStringLiteral("Giveaway winner")},
	};
	for (const auto &event : events)
		addToggle(eventsLayout, event.second, event.first, eventToggles_);
	auto *testRow = new QHBoxLayout();
	testEventType_ = new QComboBox();
	const QList<QPair<QString, QString>> testEvents = {
		{QStringLiteral("follow"), QStringLiteral("Follow")},
		{QStringLiteral("subscription"), QStringLiteral("Subscription")},
		{QStringLiteral("gift_subscription"), QStringLiteral("Gift Subscription")},
		{QStringLiteral("raid"), QStringLiteral("Raid")},
		{QStringLiteral("cheer"), QStringLiteral("Cheer / Bits")},
		{QStringLiteral("donation"), QStringLiteral("Charity Donation")},
		{QStringLiteral("redemption"), QStringLiteral("Channel Point Redemption")},
		{QStringLiteral("auto_live"), QStringLiteral("Auto Live")},
		{QStringLiteral("walk_on"), QStringLiteral("Walk-On")},
		{QStringLiteral("follow_bot_ad_removed"), QStringLiteral("Removed Follow-Bot Ad")},
		{QStringLiteral("custom_mini_game_winner"), QStringLiteral("Mini-Game Winner")},
		{QStringLiteral("giveaway_winner"), QStringLiteral("Giveaway Winner")},
	};
	for (const auto &event : testEvents)
		testEventType_->addItem(event.second, event.first);
	testEventButton_ = new QPushButton(QStringLiteral("Send Test Event"));
	testRow->addWidget(testEventType_, 1);
	testRow->addWidget(testEventButton_);
	eventsLayout->addLayout(testRow);
	eventsLayout->addStretch();
	tabs->addTab(eventsTab, QStringLiteral("Stream Events"));

	auto *overlaysTab = new QWidget();
	auto *overlaysLayout = new QVBoxLayout(overlaysTab);
	auto *overlayTabs = new QTabWidget();
	overlaysLayout->addWidget(overlayTabs);

	const auto configureColorField = [](QLineEdit *field) {
		field->setMaxLength(7);
		field->setValidator(new QRegularExpressionValidator(
			QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$")), field));
	};
	const auto configureSizeField = [](QSpinBox *field) {
		field->setRange(8, 256);
		field->setValue(64);
		field->setSuffix(QStringLiteral(" px"));
	};

	auto *winLossPane = new QWidget();
	auto *winLossForm = new QFormLayout(winLossPane);
	winLossColor_ = new QLineEdit(QStringLiteral("#AAAAAA"));
	configureColorField(winLossColor_);
	winLossSize_ = new QSpinBox();
	configureSizeField(winLossSize_);
	winLossGameTitle_ = new QCheckBox(QStringLiteral("Show game title"));
	winLossRatio_ = new QCheckBox(QStringLiteral("Show win/loss ratio"));
	winLossWinPercent_ = new QCheckBox(QStringLiteral("Show win percentage"));
	winLossLossPercent_ = new QCheckBox(QStringLiteral("Show loss percentage"));
	winLossLastGame_ = new QCheckBox(QStringLiteral("Show last game"));
	winLossForm->addRow(QStringLiteral("Color"), winLossColor_);
	winLossForm->addRow(QStringLiteral("Font size"), winLossSize_);
	winLossForm->addRow(winLossGameTitle_);
	winLossForm->addRow(winLossRatio_);
	winLossForm->addRow(winLossWinPercent_);
	winLossForm->addRow(winLossLossPercent_);
	winLossForm->addRow(winLossLastGame_);
	overlayTabs->addTab(winLossPane, QStringLiteral("Win / Loss"));

	auto *deathPane = new QWidget();
	auto *deathForm = new QFormLayout(deathPane);
	deathColor_ = new QLineEdit(QStringLiteral("#AAAAAA"));
	configureColorField(deathColor_);
	deathSize_ = new QSpinBox();
	configureSizeField(deathSize_);
	deathShowTitle_ = new QCheckBox(QStringLiteral("Show game title"));
	deathUseCustomText_ = new QCheckBox(QStringLiteral("Use custom text"));
	deathCustomText_ = new QLineEdit(QStringLiteral("💀 Deaths"));
	deathCustomText_->setMaxLength(100);
	deathCustomText_->setEnabled(false);
	deathForm->addRow(QStringLiteral("Color"), deathColor_);
	deathForm->addRow(QStringLiteral("Font size"), deathSize_);
	deathForm->addRow(deathShowTitle_);
	deathForm->addRow(deathUseCustomText_);
	deathForm->addRow(QStringLiteral("Label"), deathCustomText_);
	overlayTabs->addTab(deathPane, QStringLiteral("Deaths"));

	auto *shotPane = new QWidget();
	auto *shotForm = new QFormLayout(shotPane);
	shotColor_ = new QLineEdit(QStringLiteral("#AAAAAA"));
	configureColorField(shotColor_);
	shotSize_ = new QSpinBox();
	configureSizeField(shotSize_);
	shotCustomText_ = new QLineEdit(QStringLiteral("Shots"));
	shotCustomText_->setMaxLength(15);
	shotForm->addRow(QStringLiteral("Color"), shotColor_);
	shotForm->addRow(QStringLiteral("Font size"), shotSize_);
	shotForm->addRow(QStringLiteral("Label"), shotCustomText_);
	overlayTabs->addTab(shotPane, QStringLiteral("Shots"));

	auto *creditsPane = new QWidget();
	auto *creditsForm = new QFormLayout(creditsPane);
	creditsTitlesMode_ = new QComboBox();
	creditsTitlesMode_->addItem(QStringLiteral("None"), QStringLiteral("none"));
	creditsTitlesMode_->addItem(QStringLiteral("Latest"), QStringLiteral("latest"));
	creditsTitlesMode_->addItem(QStringLiteral("All"), QStringLiteral("all"));
	creditsTitlesMode_->setCurrentIndex(1);
	creditsShowAvg_ = new QCheckBox(QStringLiteral("Show average viewers"));
	creditsShowAvg_->setChecked(true);
	creditsShowBoxArt_ = new QCheckBox(QStringLiteral("Show game box art"));
	creditsShowBoxArt_->setChecked(true);
	creditsScrollSpeed_ = new QSpinBox();
	creditsScrollSpeed_->setRange(10, 200);
	creditsScrollSpeed_->setValue(40);
	creditsScrollSpeed_->setSuffix(QStringLiteral(" px/sec"));
	creditsBannerText_ = new QLineEdit();
	creditsBannerText_->setMaxLength(200);
	creditsBannerPosition_ = new QComboBox();
	creditsBannerPosition_->addItem(QStringLiteral("Top"), QStringLiteral("top"));
	creditsBannerPosition_->addItem(QStringLiteral("Bottom"), QStringLiteral("bottom"));
	creditsForm->addRow(QStringLiteral("Titles"), creditsTitlesMode_);
	creditsForm->addRow(creditsShowAvg_);
	creditsForm->addRow(creditsShowBoxArt_);
	creditsForm->addRow(QStringLiteral("Scroll speed"), creditsScrollSpeed_);
	creditsForm->addRow(QStringLiteral("Banner text"), creditsBannerText_);
	creditsForm->addRow(QStringLiteral("Banner position"), creditsBannerPosition_);
	overlayTabs->addTab(creditsPane, QStringLiteral("Credits"));
	tabs->addTab(overlaysTab, QStringLiteral("Overlays"));

	auto *sourcesTab = new QWidget();
	auto *sourcesLayout = new QVBoxLayout(sourcesTab);
	auto *sourcesHelp = new QLabel(QStringLiteral(
		"Install or update Bacons Helper Browser Sources in the current OBS scene. "
		"Existing sources keep their scene transforms while their URL is synchronized."));
	sourcesHelp->setWordWrap(true);
	sourcesLayout->addWidget(sourcesHelp);
	const QList<QPair<QString, QString>> browserSources = {
		{QStringLiteral("streamEvents"), QStringLiteral("Stream Events")},
		{QStringLiteral("countdown"), QStringLiteral("Countdown")},
		{QStringLiteral("winLoss"), QStringLiteral("Win / Loss")},
		{QStringLiteral("deathCounter"), QStringLiteral("Death Counter")},
		{QStringLiteral("shotCounter"), QStringLiteral("Shot Counter")},
		{QStringLiteral("credits"), QStringLiteral("Credits")},
	};
	for (const auto &source : browserSources) {
		auto *button = new QPushButton(QStringLiteral("Install / Update %1").arg(source.second));
		sourceInstallButtons_.insert(source.first, button);
		connect(button, &QPushButton::clicked, this,
				[this, key = source.first] { installBrowserSource(key); });
		sourcesLayout->addWidget(button);
	}
	sourceStatus_ = new QLabel(QStringLiteral("Refresh after pairing to load Browser Source URLs."));
	sourceStatus_->setWordWrap(true);
	sourcesLayout->addWidget(sourceStatus_);
	sourcesLayout->addStretch();
	tabs->addTab(sourcesTab, QStringLiteral("OBS Sources"));

	auto *countdownTab = new QWidget();
	auto *countdownLayout = new QVBoxLayout(countdownTab);
	auto *countdownRow = new QHBoxLayout();
	countdownSeconds_ = new QSpinBox();
	countdownSeconds_->setRange(1, 86400);
	countdownSeconds_->setValue(60);
	countdownSeconds_->setSuffix(QStringLiteral(" seconds"));
	countdownStartButton_ = new QPushButton(QStringLiteral("Start Countdown"));
	countdownCancelButton_ = new QPushButton(QStringLiteral("Cancel"));
	countdownRow->addWidget(countdownSeconds_);
	countdownRow->addWidget(countdownStartButton_);
	countdownRow->addWidget(countdownCancelButton_);
	auto *presetRow = new QHBoxLayout();
	countdownPreset30_ = new QPushButton(QStringLiteral("30s"));
	countdownPreset60_ = new QPushButton(QStringLiteral("1m"));
	countdownPreset120_ = new QPushButton(QStringLiteral("2m"));
	countdownPreset300_ = new QPushButton(QStringLiteral("5m"));
	presetRow->addWidget(countdownPreset30_);
	presetRow->addWidget(countdownPreset60_);
	presetRow->addWidget(countdownPreset120_);
	presetRow->addWidget(countdownPreset300_);
	countdownStatus_ = new QLabel(QStringLiteral("Countdown: inactive"));
	countdownStatus_->setWordWrap(true);
	countdownLayout->addLayout(countdownRow);
	countdownLayout->addLayout(presetRow);
	countdownLayout->addWidget(countdownStatus_);
	countdownLayout->addStretch();
	tabs->addTab(countdownTab, QStringLiteral("Countdown"));


	auto *liveTab = new QWidget();
	auto *liveOuter = new QVBoxLayout(liveTab);
	auto *liveScroll = new QScrollArea();
	liveScroll->setWidgetResizable(true);
	auto *liveContent = new QWidget();
	auto *liveLayout = new QVBoxLayout(liveContent);

	auto *deathControls = new QGroupBox(QStringLiteral("Death Counters"));
	auto *deathControlsLayout = new QVBoxLayout(deathControls);
	deathCounterSelect_ = new QComboBox();
	deathCounterValue_ = new QLabel(QStringLiteral("Select a saved game counter."));
	deathCounterValue_->setWordWrap(true);
	auto *deathAdjustRow = new QHBoxLayout();
	deathCounterMinusButton_ = new QPushButton(QStringLiteral("-1"));
	deathCounterPlusButton_ = new QPushButton(QStringLiteral("+1"));
	deathCounterResetButton_ = new QPushButton(QStringLiteral("Reset"));
	deathAdjustRow->addWidget(deathCounterMinusButton_);
	deathAdjustRow->addWidget(deathCounterPlusButton_);
	deathAdjustRow->addWidget(deathCounterResetButton_);
	auto *deathSetRow = new QHBoxLayout();
	deathCounterSetValue_ = new QSpinBox();
	deathCounterSetValue_->setRange(0, 999999);
	deathCounterSetButton_ = new QPushButton(QStringLiteral("Set Count"));
	deathSetRow->addWidget(deathCounterSetValue_, 1);
	deathSetRow->addWidget(deathCounterSetButton_);
	deathControlsLayout->addWidget(deathCounterSelect_);
	deathControlsLayout->addWidget(deathCounterValue_);
	deathControlsLayout->addLayout(deathAdjustRow);
	deathControlsLayout->addLayout(deathSetRow);
	liveLayout->addWidget(deathControls);

	auto *loyaltyControls = new QGroupBox(QStringLiteral("Loyalty Points"));
	auto *loyaltyLayout = new QFormLayout(loyaltyControls);
	loyaltyEnabled_ = new QCheckBox(QStringLiteral("Enable Loyalty Points"));
	loyaltyCurrency_ = new QLineEdit(QStringLiteral("Points"));
	loyaltyCurrency_->setMaxLength(40);
	loyaltyAmount_ = new QSpinBox();
	loyaltyAmount_->setRange(0, 1000000000);
	loyaltyInterval_ = new QSpinBox();
	loyaltyInterval_->setRange(3, 10);
	loyaltyInterval_->setSuffix(QStringLiteral(" min"));
	loyaltyTicketCost_ = new QSpinBox();
	loyaltyTicketCost_->setRange(1, 1000000000);
	loyaltyMaxTickets_ = new QSpinBox();
	loyaltyMaxTickets_->setRange(0, 100000);
	loyaltyMaxTickets_->setSpecialValueText(QStringLiteral("Unlimited"));
	loyaltyRafflePoints_ = new QSpinBox();
	loyaltyRafflePoints_->setRange(1, 1000000000);
	loyaltyRaffleDuration_ = new QSpinBox();
	loyaltyRaffleDuration_->setRange(10, 3600);
	loyaltyRaffleDuration_->setSuffix(QStringLiteral(" sec"));
	loyaltySaveButton_ = new QPushButton(QStringLiteral("Save Loyalty Settings"));
	loyaltySummary_ = new QLabel(QStringLiteral("Loyalty data will appear after refresh."));
	loyaltySummary_->setWordWrap(true);
	loyaltyLayout->addRow(loyaltyEnabled_);
	loyaltyLayout->addRow(QStringLiteral("Currency"), loyaltyCurrency_);
	loyaltyLayout->addRow(QStringLiteral("Points / interval"), loyaltyAmount_);
	loyaltyLayout->addRow(QStringLiteral("Award interval"), loyaltyInterval_);
	loyaltyLayout->addRow(QStringLiteral("Ticket cost"), loyaltyTicketCost_);
	loyaltyLayout->addRow(QStringLiteral("Max tickets"), loyaltyMaxTickets_);
	loyaltyLayout->addRow(QStringLiteral("Raffle award"), loyaltyRafflePoints_);
	loyaltyLayout->addRow(QStringLiteral("Raffle duration"), loyaltyRaffleDuration_);
	loyaltyLayout->addRow(loyaltySaveButton_);
	loyaltyLayout->addRow(loyaltySummary_);
	liveLayout->addWidget(loyaltyControls);

	auto *giveawayControls = new QGroupBox(QStringLiteral("Giveaway / Raffle"));
	auto *giveawayLayout = new QVBoxLayout(giveawayControls);
	giveawayStatus_ = new QLabel(QStringLiteral("No giveaway state loaded."));
	giveawayStatus_->setWordWrap(true);
	giveawayName_ = new QLineEdit();
	giveawayName_->setMaxLength(100);
	giveawayName_->setPlaceholderText(QStringLiteral("Giveaway name"));
	auto *giveawayLifecycle = new QHBoxLayout();
	giveawayStartButton_ = new QPushButton(QStringLiteral("Start"));
	giveawayPauseButton_ = new QPushButton(QStringLiteral("Pause"));
	giveawayResumeButton_ = new QPushButton(QStringLiteral("Resume"));
	giveawayEndButton_ = new QPushButton(QStringLiteral("End"));
	giveawayLifecycle->addWidget(giveawayStartButton_);
	giveawayLifecycle->addWidget(giveawayPauseButton_);
	giveawayLifecycle->addWidget(giveawayResumeButton_);
	giveawayLifecycle->addWidget(giveawayEndButton_);
	auto *winnerRow = new QHBoxLayout();
	giveawayWinnerCount_ = new QSpinBox();
	giveawayWinnerCount_->setRange(1, 10);
	giveawayPickButton_ = new QPushButton(QStringLiteral("Pick Winner(s)"));
	winnerRow->addWidget(giveawayWinnerCount_);
	winnerRow->addWidget(giveawayPickButton_, 1);
	auto *raffleRow = new QHBoxLayout();
	raffleStartButton_ = new QPushButton(QStringLiteral("Start Raffle"));
	raffleCancelButton_ = new QPushButton(QStringLiteral("Cancel Raffle"));
	raffleRow->addWidget(raffleStartButton_);
	raffleRow->addWidget(raffleCancelButton_);
	giveawayLayout->addWidget(giveawayStatus_);
	giveawayLayout->addWidget(giveawayName_);
	giveawayLayout->addLayout(giveawayLifecycle);
	giveawayLayout->addLayout(winnerRow);
	giveawayLayout->addLayout(raffleRow);
	liveLayout->addWidget(giveawayControls);

	auto *walkOnControls = new QGroupBox(QStringLiteral("Walk-Ons"));
	auto *walkOnLayout = new QFormLayout(walkOnControls);
	walkOnSelect_ = new QComboBox();
	walkOnSummary_ = new QLabel(QStringLiteral("Select a configured Walk-On user."));
	walkOnSummary_->setWordWrap(true);
	walkOnEnabled_ = new QCheckBox(QStringLiteral("Enabled"));
	walkOnOverlay_ = new QCheckBox(QStringLiteral("Show Stream Events overlay"));
	walkOnShoutout_ = new QCheckBox(QStringLiteral("Automatic shoutout"));
	walkOnAnnouncement_ = new QCheckBox(QStringLiteral("Chat announcement"));
	walkOnColor_ = new QComboBox();
	for (const QString &color : {QStringLiteral("primary"), QStringLiteral("blue"), QStringLiteral("green"),
								QStringLiteral("orange"), QStringLiteral("purple")})
		walkOnColor_->addItem(color.left(1).toUpper() + color.mid(1), color);
	walkOnSaveButton_ = new QPushButton(QStringLiteral("Save Walk-On Settings"));
	walkOnTestButton_ = new QPushButton(QStringLiteral("Preview Overlay"));
	auto *walkOnButtons = new QWidget();
	auto *walkOnButtonsLayout = new QHBoxLayout(walkOnButtons);
	walkOnButtonsLayout->setContentsMargins(0, 0, 0, 0);
	walkOnButtonsLayout->addWidget(walkOnSaveButton_);
	walkOnButtonsLayout->addWidget(walkOnTestButton_);
	walkOnLayout->addRow(QStringLiteral("User"), walkOnSelect_);
	walkOnLayout->addRow(walkOnSummary_);
	walkOnLayout->addRow(walkOnEnabled_);
	walkOnLayout->addRow(walkOnOverlay_);
	walkOnLayout->addRow(walkOnShoutout_);
	walkOnLayout->addRow(walkOnAnnouncement_);
	walkOnLayout->addRow(QStringLiteral("Announcement color"), walkOnColor_);
	walkOnLayout->addRow(walkOnButtons);
	liveLayout->addWidget(walkOnControls);

	liveLayout->addStretch();
	liveScroll->setWidget(liveContent);
	liveOuter->addWidget(liveScroll);
	tabs->addTab(liveTab, QStringLiteral("Live Controls"));

	auto *toolsTab = new QWidget();
	auto *toolsLayout = new QVBoxLayout(toolsTab);
	const QList<QPair<QString, QString>> tools = {
		{QStringLiteral("dashboard"), QStringLiteral("Open full Dashboard")},
		{QStringLiteral("overlays"), QStringLiteral("OBS Overlays")},
		{QStringLiteral("miniGame"), QStringLiteral("Custom Mini-Game")},
		{QStringLiteral("timedActions"), QStringLiteral("Timed Actions")},
		{QStringLiteral("giveaway"), QStringLiteral("Loyalty / Giveaway")},
		{QStringLiteral("walkOns"), QStringLiteral("Walk-On Messages")},
	};
	for (const auto &tool : tools) {
		auto *button = new QPushButton(tool.second);
		connect(button, &QPushButton::clicked, this, [this, key = tool.first] { openDashboardLink(key); });
		toolsLayout->addWidget(button);
	}
	toolsLayout->addStretch();
	tabs->addTab(toolsTab, QStringLiteral("More"));

	connect(pairButton_, &QPushButton::clicked, this, &BaconsHelperDock::pair);
	connect(disconnectButton_, &QPushButton::clicked, this, &BaconsHelperDock::disconnectAccount);
	connect(refreshButton_, &QPushButton::clicked, this, &BaconsHelperDock::refresh);
	connect(countdownStartButton_, &QPushButton::clicked, this, &BaconsHelperDock::startCountdown);
	connect(countdownCancelButton_, &QPushButton::clicked, this, &BaconsHelperDock::cancelCountdown);
	connect(countdownPreset30_, &QPushButton::clicked, this, [this] { startCountdownPreset(30); });
	connect(countdownPreset60_, &QPushButton::clicked, this, [this] { startCountdownPreset(60); });
	connect(countdownPreset120_, &QPushButton::clicked, this, [this] { startCountdownPreset(120); });
	connect(countdownPreset300_, &QPushButton::clicked, this, [this] { startCountdownPreset(300); });
	connect(testEventButton_, &QPushButton::clicked, this, &BaconsHelperDock::testStreamEvent);
	connect(deathCounterSelect_, &QComboBox::currentIndexChanged, this, [this](int) { updateDeathCounterSelection(); });
	connect(deathCounterMinusButton_, &QPushButton::clicked, this, [this] { runDeathCounterAction(QStringLiteral("decrement")); });
	connect(deathCounterPlusButton_, &QPushButton::clicked, this, [this] { runDeathCounterAction(QStringLiteral("increment")); });
	connect(deathCounterResetButton_, &QPushButton::clicked, this, [this] { runDeathCounterAction(QStringLiteral("reset")); });
	connect(deathCounterSetButton_, &QPushButton::clicked, this, [this] { runDeathCounterAction(QStringLiteral("set"), deathCounterSetValue_->value()); });
	connect(loyaltyEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
		if (!applyingState_) setLoyaltyActive(enabled);
	});
	connect(loyaltySaveButton_, &QPushButton::clicked, this, &BaconsHelperDock::saveLoyaltySettings);
	connect(giveawayStartButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("start")); });
	connect(giveawayPauseButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("pause")); });
	connect(giveawayResumeButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("resume")); });
	connect(giveawayEndButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("end")); });
	connect(giveawayPickButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("pick-winners")); });
	connect(raffleStartButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("start-raffle")); });
	connect(raffleCancelButton_, &QPushButton::clicked, this, [this] { runGiveawayAction(QStringLiteral("cancel-raffle")); });
	connect(walkOnSelect_, &QComboBox::currentIndexChanged, this, [this](int) { updateWalkOnSelection(); });
	connect(walkOnSaveButton_, &QPushButton::clicked, this, &BaconsHelperDock::saveWalkOnSettings);
	connect(walkOnTestButton_, &QPushButton::clicked, this, &BaconsHelperDock::testWalkOn);

	for (auto it = channelToggles_.begin(); it != channelToggles_.end(); ++it) {
		const QString key = it.key();
		connect(it.value(), &QCheckBox::toggled, this, [this, key](bool checked) {
			if (!applyingState_)
				saveChannelToggle(key, checked);
		});
	}
	for (auto it = eventToggles_.begin(); it != eventToggles_.end(); ++it) {
		const QString key = it.key();
		connect(it.value(), &QCheckBox::toggled, this, [this, key](bool checked) {
			if (!applyingState_)
				saveStreamEvent(key, checked);
		});
	}

	const auto saveOverlayOnToggle = [this](QCheckBox *toggle, const QString &key) {
		connect(toggle, &QCheckBox::toggled, this, [this, key](bool) {
			if (!applyingState_) saveOverlaySettings(key);
		});
	};
	const auto saveOverlayOnSpin = [this](QSpinBox *spin, const QString &key) {
		connect(spin, &QSpinBox::valueChanged, this, [this, key](int) {
			if (!applyingState_) saveOverlaySettings(key);
		});
	};
	const auto saveOverlayOnEdit = [this](QLineEdit *edit, const QString &key) {
		connect(edit, &QLineEdit::editingFinished, this, [this, key, edit] {
			if (!applyingState_ && edit->hasAcceptableInput()) saveOverlaySettings(key);
		});
	};

	for (auto *toggle : {winLossGameTitle_, winLossRatio_, winLossWinPercent_, winLossLossPercent_, winLossLastGame_})
		saveOverlayOnToggle(toggle, QStringLiteral("winloss"));
	saveOverlayOnSpin(winLossSize_, QStringLiteral("winloss"));
	saveOverlayOnEdit(winLossColor_, QStringLiteral("winloss"));
	for (auto *toggle : {deathShowTitle_, deathUseCustomText_})
		saveOverlayOnToggle(toggle, QStringLiteral("deathcounter"));
	connect(deathUseCustomText_, &QCheckBox::toggled, deathCustomText_, &QWidget::setEnabled);
	saveOverlayOnSpin(deathSize_, QStringLiteral("deathcounter"));
	saveOverlayOnEdit(deathColor_, QStringLiteral("deathcounter"));
	saveOverlayOnEdit(deathCustomText_, QStringLiteral("deathcounter"));
	saveOverlayOnSpin(shotSize_, QStringLiteral("shotcounter"));
	saveOverlayOnEdit(shotColor_, QStringLiteral("shotcounter"));
	saveOverlayOnEdit(shotCustomText_, QStringLiteral("shotcounter"));
	saveOverlayOnToggle(creditsShowAvg_, QStringLiteral("credits"));
	saveOverlayOnToggle(creditsShowBoxArt_, QStringLiteral("credits"));
	saveOverlayOnSpin(creditsScrollSpeed_, QStringLiteral("credits"));
	saveOverlayOnEdit(creditsBannerText_, QStringLiteral("credits"));
	connect(creditsTitlesMode_, &QComboBox::currentTextChanged, this, [this](const QString &) {
		if (!applyingState_) saveOverlaySettings(QStringLiteral("credits"));
	});
	connect(creditsBannerPosition_, &QComboBox::currentTextChanged, this, [this](const QString &) {
		if (!applyingState_) saveOverlaySettings(QStringLiteral("credits"));
	});

	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(30000);
	connect(refreshTimer_, &QTimer::timeout, this, [this] {
		if (!token_.isEmpty()) refresh();
	});
	refreshTimer_->start();
	countdownTickTimer_ = new QTimer(this);
	countdownTickTimer_->setInterval(1000);
	connect(countdownTickTimer_, &QTimer::timeout, this, &BaconsHelperDock::updateCountdownStatus);
	countdownTickTimer_->start();
}

void BaconsHelperDock::loadLocalSettings()
{
	QSettings settings(configPath(), QSettings::IniFormat);
	const QString server = settings.value(QStringLiteral("server"), QString::fromLatin1(kDefaultServer)).toString();
	serverUrl_->setText(server);

#ifdef BH_HAVE_LIBSECRET
	QString lookupError;
	token_ = secretServiceLookup(server, &lookupError);
	if (!lookupError.isEmpty())
		credentialStorageWarning_ = lookupError;

	// Migrate credentials written by the early Linux prototype. "scoped:" was
	// only Base64 and therefore was not encrypted at rest.
	const QString legacy = settings.value(QStringLiteral("credential")).toString();
	if (legacy.startsWith(QStringLiteral("scoped:"))) {
		const QString migrated = QString::fromUtf8(QByteArray::fromBase64(legacy.mid(7).toLatin1()));
		// Never leave the reversible legacy value on disk, even when a Secret
		// Service entry already exists or the legacy value is malformed.
		settings.remove(QStringLiteral("credential"));
		settings.sync();
		if (token_.isEmpty() && !migrated.isEmpty()) {
			QString migrationError;
			if (secretServiceStore(server, migrated, &migrationError)) {
				token_ = migrated;
				credentialStorageWarning_.clear();
				settings.setValue(QStringLiteral("credential"), QStringLiteral("secret-service"));
				settings.sync();
			} else {
				// Keep it in memory for this OBS session so the user can repair
				// their keyring or re-pair; it is no longer persisted insecurely.
				token_ = migrated;
				credentialStorageWarning_ = migrationError;
			}
		}
	}
#else
	const QString storedCredential = settings.value(QStringLiteral("credential")).toString();
#ifdef _WIN32
	token_ = unprotectToken(storedCredential);
#else
	// Session-only Linux/Flatpak builds must not retain the reversible Base64
	// credential used by the early prototype. Remove it on sight.
	if (storedCredential.startsWith(QStringLiteral("scoped:"))) {
		settings.remove(QStringLiteral("credential"));
		settings.sync();
	}
	token_.clear();
#endif
#endif
}

bool BaconsHelperDock::saveLocalSettings(QString *credentialError)
{
	QSettings settings(configPath(), QSettings::IniFormat);
	const QString previousServer = settings.value(QStringLiteral("server"), QString::fromLatin1(kDefaultServer)).toString();
	const QString server = serverUrl_->text().trimmed();
	settings.setValue(QStringLiteral("server"), server);

#ifdef BH_HAVE_LIBSECRET
	bool credentialSaved = true;
	if (token_.isEmpty()) {
		QString clearError;
		credentialSaved = secretServiceClear(server, &clearError);
		if (previousServer != server) {
			QString previousClearError;
			if (!secretServiceClear(previousServer, &previousClearError) && credentialSaved) {
				credentialSaved = false;
				clearError = previousClearError;
			}
		}
		settings.remove(QStringLiteral("credential"));
		if (!credentialSaved && credentialError)
			*credentialError = clearError;
	} else {
		if (previousServer != server)
			secretServiceClear(previousServer, nullptr);
		QString storeError;
		credentialSaved = secretServiceStore(server, token_, &storeError);
		if (credentialSaved) {
			// Marker only; the credential itself lives in Secret Service.
			settings.setValue(QStringLiteral("credential"), QStringLiteral("secret-service"));
		} else {
			// Fail closed: do not put a reversible credential in QSettings.
			settings.remove(QStringLiteral("credential"));
			if (credentialError)
				*credentialError = storeError;
		}
	}
	settings.sync();
	return credentialSaved;
#else
	const QString protectedToken = protectToken(token_);
	if (!token_.isEmpty() && protectedToken.isEmpty()) {
		settings.remove(QStringLiteral("credential"));
		settings.sync();
		if (credentialError)
			*credentialError = QStringLiteral("The operating system credential store was unavailable.");
		return false;
	}
	settings.setValue(QStringLiteral("credential"), protectedToken);
	settings.sync();
	return true;
#endif
}

void BaconsHelperDock::setStatus(const QString &message, bool connected)
{
	status_->setText(message);
	status_->setToolTip(connected ? QStringLiteral("Bacons Helper is reachable and authenticated")
								  : QStringLiteral("Bacons Helper is not currently authenticated"));
}

void BaconsHelperDock::setControlsEnabled(bool enabled)
{
	// A paired credential is scoped to the server it came from. Lock the
	// endpoint while paired so the bearer credential cannot be redirected to a
	// different HTTPS host. Forget the pairing before changing servers.
	serverUrl_->setEnabled(!enabled);
	pairCode_->setEnabled(!enabled);
	pairButton_->setEnabled(!enabled);

	for (auto *toggle : channelToggles_)
		toggle->setEnabled(enabled);
	for (auto *toggle : eventToggles_)
		toggle->setEnabled(enabled);
	testEventType_->setEnabled(enabled);
	testEventButton_->setEnabled(enabled);
	for (auto *button : sourceInstallButtons_)
		button->setEnabled(enabled);
	countdownSeconds_->setEnabled(enabled);
	for (auto *button : {countdownStartButton_, countdownCancelButton_, countdownPreset30_,
					 countdownPreset60_, countdownPreset120_, countdownPreset300_})
		button->setEnabled(enabled);
	const QList<QWidget *> liveControls = {
		deathCounterSelect_, deathCounterSetValue_, deathCounterMinusButton_, deathCounterPlusButton_,
		deathCounterSetButton_, deathCounterResetButton_, loyaltyEnabled_, loyaltyCurrency_, loyaltyAmount_,
		loyaltyInterval_, loyaltyTicketCost_, loyaltyMaxTickets_, loyaltyRafflePoints_, loyaltyRaffleDuration_,
		loyaltySaveButton_, giveawayName_, giveawayWinnerCount_, giveawayStartButton_, giveawayPauseButton_,
		giveawayResumeButton_, giveawayEndButton_, giveawayPickButton_, raffleStartButton_, raffleCancelButton_,
		walkOnSelect_, walkOnEnabled_, walkOnOverlay_, walkOnShoutout_, walkOnAnnouncement_, walkOnColor_,
		walkOnSaveButton_, walkOnTestButton_};
	for (QWidget *control : liveControls)
		control->setEnabled(enabled);
	const QList<QWidget *> overlayControls = {
			 winLossColor_, winLossSize_, winLossGameTitle_, winLossRatio_, winLossWinPercent_,
			 winLossLossPercent_, winLossLastGame_, deathColor_, deathSize_, deathShowTitle_,
			 deathUseCustomText_, deathCustomText_, shotColor_, shotSize_, shotCustomText_,
			 creditsTitlesMode_, creditsShowAvg_, creditsShowBoxArt_, creditsScrollSpeed_,
			 creditsBannerText_, creditsBannerPosition_};
	for (QWidget *control : overlayControls) {
		control->setEnabled(enabled);
	}
	if (enabled)
		deathCustomText_->setEnabled(deathUseCustomText_->isChecked());
	updateDeathCounterSelection();
	updateWalkOnSelection();
	refreshButton_->setEnabled(enabled);
	disconnectButton_->setEnabled(enabled);
}

bool BaconsHelperDock::validateServerUrl()
{
	QUrl url(serverUrl_->text().trimmed());
	if (!url.isValid() || url.scheme().toLower() != QStringLiteral("https") || url.host().isEmpty() ||
		!url.userInfo().isEmpty()) {
		setStatus(QStringLiteral("⚠ Server URL must be a valid HTTPS address without embedded credentials."), false);
		return false;
	}
	serverUrl_->setText(url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::StripTrailingSlash).toString());
	return true;
}

QUrl BaconsHelperDock::endpoint(const QString &path) const
{
	QUrl base(serverUrl_->text().trimmed());
	base.setPath(path.startsWith('/') ? path : QStringLiteral("/") + path);
	base.setQuery(QString{});
	base.setFragment(QString{});
	return base;
}

QNetworkReply *BaconsHelperDock::sendJson(const QByteArray &method, const QString &path, const QJsonObject &payload)
{
	QNetworkRequest request(endpoint(path));
	// Never allow an authenticated request to follow a redirect to another
	// origin. This keeps the channel-scoped bearer credential pinned to the
	// server the user paired with.
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("User-Agent", "Bacons-Helper-OBS/" BH_OBS_VERSION);
	if (!token_.isEmpty())
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());

	const QByteArray body = payload.isEmpty() ? QByteArray() : QJsonDocument(payload).toJson(QJsonDocument::Compact);
	if (method == "GET")
		return network_->get(request);
	if (method == "POST")
		return network_->post(request, body);
	if (method == "PUT")
		return network_->put(request, body);
	return network_->sendCustomRequest(request, method, body);
}

void BaconsHelperDock::handleAuthFailure(QNetworkReply *reply)
{
	if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
		token_.clear();
		browserSourceUrls_.clear();
		saveLocalSettings();
		setControlsEnabled(false);
		channel_->setText(QStringLiteral("Channel: pairing required"));
		setStatus(QStringLiteral("✕ Pairing expired or was revoked. Pair this OBS installation again."), false);
	}
}

void BaconsHelperDock::pair()
{
	if (!validateServerUrl())
		return;
	if (!QSslSocket::supportsSsl()) {
		const auto tlsBackends = QSslSocket::availableBackends();
		const QString available = QStringList(tlsBackends).join(QStringLiteral(", "));
		setStatus(QStringLiteral("✕ HTTPS is unavailable because Qt has no functional TLS backend. "
							 "Available backends: %1. Reinstall the Bacons Helper OBS Companion package so its TLS files are included.")
				  .arg(available.isEmpty() ? QStringLiteral("none") : available), false);
		return;
	}
	const QString code = pairCode_->text().trimmed();
	if (code.isEmpty()) {
		setStatus(QStringLiteral("⚠ Enter the pairing code generated on your Bacons Helper dashboard."), false);
		return;
	}

	setStatus(QStringLiteral("… Pairing with Bacons Helper…"), false);
	QJsonObject payload{{QStringLiteral("code"), code},
						{QStringLiteral("deviceName"), QStringLiteral("OBS Studio %1").arg(QSysInfo::prettyProductName())}};
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/link"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray responseBody = reply->readAll();
		const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
		if (reply->error() != QNetworkReply::NoError || !doc.object().value(QStringLiteral("ok")).toBool()) {
			const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			QString serverMessage = doc.object().value(QStringLiteral("error")).toString().trimmed();
			if (serverMessage.isEmpty())
				serverMessage = reply->errorString().trimmed();
			if (serverMessage.isEmpty())
				serverMessage = QStringLiteral("The server returned an invalid response.");

			if (statusCode > 0) {
				setStatus(QStringLiteral("✕ Pairing failed (HTTP %1): %2").arg(statusCode).arg(serverMessage), false);
			} else {
				setStatus(QStringLiteral("✕ Pairing failed: %1").arg(serverMessage), false);
			}
			reply->deleteLater();
			return;
		}
		token_ = doc.object().value(QStringLiteral("token")).toString();
		if (token_.isEmpty()) {
			setStatus(QStringLiteral("✕ Bacons Helper did not return a usable credential."), false);
			reply->deleteLater();
			return;
		}
		pairCode_->clear();
		QString credentialError;
		const bool persisted = saveLocalSettings(&credentialError);
		credentialStorageWarning_ = persisted ? QString() : credentialError;
		setControlsEnabled(true);
		if (persisted)
			setStatus(QStringLiteral("✓ Paired. Loading channel settings…"), true);
		else
			setStatus(QStringLiteral("⚠ Paired for this OBS session, but secure credential storage failed: %1").arg(credentialError), false);
		reply->deleteLater();
		refresh();
	});
}

void BaconsHelperDock::disconnectAccount()
{
	// Ask Bacons Helper to revoke this installation before forgetting the local
	// token. The request already owns a copy of the Authorization header, so the
	// local credential can be cleared immediately even if the network is slow.
	QNetworkReply *revokeReply = nullptr;
	const QUrl currentServer(serverUrl_->text().trimmed());
	if (!token_.isEmpty() && currentServer.isValid() &&
		currentServer.scheme().toLower() == QStringLiteral("https") &&
		!currentServer.host().isEmpty() && currentServer.userInfo().isEmpty()) {
		revokeReply = sendJson("DELETE", QStringLiteral("/api/obs-plugin/credential"));
	}

	token_.clear();
	dashboardLinks_.clear();
	browserSourceUrls_.clear();
	deathCounters_.clear();
	walkOns_.clear();
	loyaltyState_ = QJsonObject{};
	deathCounterSelect_->clear();
	walkOnSelect_->clear();
	countdownEndTime_ = 0;
	updateCountdownStatus();
	if (sourceStatus_)
		sourceStatus_->setText(QStringLiteral("Pair again to manage Bacons Helper Browser Sources."));
	QString credentialError;
	const bool cleared = saveLocalSettings(&credentialError);
	credentialStorageWarning_ = cleared ? QString() : credentialError;
	setControlsEnabled(false);
	channel_->setText(QStringLiteral("Channel: not paired"));

	if (!revokeReply) {
		setStatus(QStringLiteral("✕ Pairing forgotten locally. The server credential could not be contacted; revoke it from the dashboard if needed."), false);
		return;
	}

	setStatus(QStringLiteral("… Pairing forgotten locally; revoking this OBS credential on Bacons Helper…"), false);
	connect(revokeReply, &QNetworkReply::finished, this, [this, revokeReply, cleared, credentialError] {
		const QByteArray bytes = revokeReply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		const bool revoked = revokeReply->error() == QNetworkReply::NoError &&
			response.value(QStringLiteral("ok")).toBool();
		if (revoked && cleared) {
			setStatus(QStringLiteral("✓ Pairing revoked on Bacons Helper and forgotten on this computer."), false);
		} else if (revoked) {
			setStatus(QStringLiteral("⚠ Server pairing was revoked, but local credential cleanup reported: %1").arg(credentialError), false);
		} else {
			setStatus(QStringLiteral("⚠ Pairing was forgotten locally, but server revocation could not be confirmed. Revoke it from the dashboard if needed."), false);
		}
		revokeReply->deleteLater();
	});
}

void BaconsHelperDock::refresh()
{
	if (token_.isEmpty() || !validateServerUrl())
		return;
	setStatus(QStringLiteral("… Refreshing Bacons Helper settings…"), true);
	auto *reply = sendJson("GET", QStringLiteral("/api/obs-plugin/settings"));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Unable to refresh. Existing settings remain visible."), false);
			reply->deleteLater();
			return;
		}
		const QJsonDocument doc = QJsonDocument::fromJson(bytes);
		if (!doc.isObject() || !doc.object().value(QStringLiteral("ok")).toBool()) {
			setStatus(QStringLiteral("⚠ Bacons Helper returned an unexpected response."), false);
			reply->deleteLater();
			return;
		}
		applySettingsPayload(doc.object());
		if (!credentialStorageWarning_.isEmpty()) {
			setStatus(QStringLiteral("⚠ Connected for this OBS session, but the pairing cannot be stored securely: %1")
					  .arg(credentialStorageWarning_), false);
		} else {
			setStatus(QStringLiteral("✓ Connected — settings are synchronized with Bacons Helper."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::applySettingsPayload(const QJsonObject &payload)
{
	applyingState_ = true;
	const QJsonObject channel = payload.value(QStringLiteral("channel")).toObject();
	channel_->setText(QStringLiteral("Channel: %1").arg(channel.value(QStringLiteral("displayName")).toString(channel.value(QStringLiteral("login")).toString())));

	const QJsonObject settings = payload.value(QStringLiteral("settings")).toObject();
	for (auto it = channelToggles_.begin(); it != channelToggles_.end(); ++it)
		it.value()->setChecked(settings.value(it.key()).toBool());

	const QJsonObject streamEvents = payload.value(QStringLiteral("streamEvents")).toObject();
	const QJsonObject events = streamEvents.value(QStringLiteral("events")).toObject();
	for (auto it = eventToggles_.begin(); it != eventToggles_.end(); ++it) {
		it.value()->setChecked(events.value(it.key()).toBool());
		it.value()->setEnabled(true);
		it.value()->setToolTip(QString{});
	}
	for (const auto &value : streamEvents.value(QStringLiteral("definitions")).toArray()) {
		const QJsonObject definition = value.toObject();
		const QString key = definition.value(QStringLiteral("key")).toString();
		auto *toggle = eventToggles_.value(key, nullptr);
		if (!toggle)
			continue;
		if (definition.value(QStringLiteral("disabled")).toBool()) {
			toggle->setEnabled(false);
			toggle->setToolTip(definition.value(QStringLiteral("unavailableReason")).toString(definition.value(QStringLiteral("description")).toString()));
		}
	}

	overlaySettings_.clear();
	const QJsonObject overlays = payload.value(QStringLiteral("overlays")).toObject();
	const auto readOverlay = [this, &overlays](const QString &key) {
		const QJsonObject settings = overlays.value(key).toObject();
		overlaySettings_.insert(key, settings);
		return settings;
	};
	const QJsonObject winLoss = readOverlay(QStringLiteral("winloss"));
	if (!winLoss.isEmpty()) {
		winLossColor_->setText(winLoss.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		winLossSize_->setValue(winLoss.value(QStringLiteral("size")).toString(QStringLiteral("64")).toInt());
		winLossGameTitle_->setChecked(winLoss.value(QStringLiteral("show_game_title")).toBool());
		winLossRatio_->setChecked(winLoss.value(QStringLiteral("show_ratio")).toBool());
		winLossWinPercent_->setChecked(winLoss.value(QStringLiteral("show_win_percent")).toBool());
		winLossLossPercent_->setChecked(winLoss.value(QStringLiteral("show_loss_percent")).toBool());
		winLossLastGame_->setChecked(winLoss.value(QStringLiteral("show_last_game")).toBool());
	}
	const QJsonObject deaths = readOverlay(QStringLiteral("deathcounter"));
	if (!deaths.isEmpty()) {
		deathColor_->setText(deaths.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		deathSize_->setValue(deaths.value(QStringLiteral("size")).toString(QStringLiteral("64")).toInt());
		deathShowTitle_->setChecked(deaths.value(QStringLiteral("show_title")).toBool());
		deathUseCustomText_->setChecked(deaths.value(QStringLiteral("use_custom_text")).toBool());
		deathCustomText_->setText(deaths.value(QStringLiteral("custom_text")).toString(QStringLiteral("💀 Deaths")));
		deathCustomText_->setEnabled(deathUseCustomText_->isChecked());
	}
	const QJsonObject shots = readOverlay(QStringLiteral("shotcounter"));
	if (!shots.isEmpty()) {
		shotColor_->setText(shots.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		shotSize_->setValue(shots.value(QStringLiteral("size")).toString(QStringLiteral("64")).toInt());
		shotCustomText_->setText(shots.value(QStringLiteral("custom_text")).toString(QStringLiteral("Shots")));
	}
	const QJsonObject credits = readOverlay(QStringLiteral("credits"));
	if (!credits.isEmpty()) {
		const int titlesIndex = creditsTitlesMode_->findData(credits.value(QStringLiteral("titlesMode")).toString(QStringLiteral("latest")));
		creditsTitlesMode_->setCurrentIndex(titlesIndex >= 0 ? titlesIndex : 1);
		creditsShowAvg_->setChecked(credits.value(QStringLiteral("showAvgViewers")).toBool(true));
		creditsShowBoxArt_->setChecked(credits.value(QStringLiteral("showBoxArt")).toBool(true));
		creditsScrollSpeed_->setValue(credits.value(QStringLiteral("scrollSpeedPx")).toInt(40));
		creditsBannerText_->setText(credits.value(QStringLiteral("bannerText")).toString());
		const int bannerIndex = creditsBannerPosition_->findData(credits.value(QStringLiteral("bannerPos")).toString(QStringLiteral("top")));
		creditsBannerPosition_->setCurrentIndex(bannerIndex >= 0 ? bannerIndex : 0);
	}

	const QJsonObject countdown = payload.value(QStringLiteral("countdown")).toObject();
	countdownEndTime_ = qRound64(countdown.value(QStringLiteral("endTime")).toDouble());
	updateCountdownStatus();

	const QJsonObject liveControls = payload.value(QStringLiteral("liveControls")).toObject();
	botIsModerator_ = liveControls.value(QStringLiteral("capabilities")).toObject()
		.value(QStringLiteral("botIsModerator")).toBool(false);
	loyaltyEnabled_->setToolTip(botIsModerator_ ? QString{}
		: QStringLiteral("Bacons Helper must be a moderator before Loyalty Points can be enabled."));
	const QString previousDeathCounter = deathCounterSelect_->currentData().toString();
	deathCounters_.clear();
	deathCounterSelect_->clear();
	for (const auto &value : liveControls.value(QStringLiteral("deathCounters")).toArray()) {
		const QJsonObject counter = value.toObject();
		const QString counterId = counter.value(QStringLiteral("counterId")).toString();
		if (counterId.isEmpty())
			continue;
		deathCounters_.insert(counterId, counter);
		deathCounterSelect_->addItem(
			QStringLiteral("%1 — %2").arg(counter.value(QStringLiteral("gameName")).toString(QStringLiteral("Unknown game")))
				.arg(counter.value(QStringLiteral("total")).toInt()),
			counterId);
	}
	const int previousDeathIndex = deathCounterSelect_->findData(previousDeathCounter);
	if (previousDeathIndex >= 0)
		deathCounterSelect_->setCurrentIndex(previousDeathIndex);
	updateDeathCounterSelection();

	applyLoyaltyState(liveControls.value(QStringLiteral("loyalty")).toObject());

	const QString previousWalkOn = walkOnSelect_->currentData().toString();
	walkOns_.clear();
	walkOnSelect_->clear();
	for (const auto &value : liveControls.value(QStringLiteral("walkOns")).toArray()) {
		const QJsonObject walkOn = value.toObject();
		const QString userId = walkOn.value(QStringLiteral("userId")).toString();
		if (userId.isEmpty())
			continue;
		walkOns_.insert(userId, walkOn);
		const QString displayName = walkOn.value(QStringLiteral("displayName")).toString(
			walkOn.value(QStringLiteral("login")).toString(userId));
		walkOnSelect_->addItem(displayName, userId);
	}
	const int previousWalkOnIndex = walkOnSelect_->findData(previousWalkOn);
	if (previousWalkOnIndex >= 0)
		walkOnSelect_->setCurrentIndex(previousWalkOnIndex);
	updateWalkOnSelection();

	browserSourceUrls_.clear();
	const QJsonObject browserSources = payload.value(QStringLiteral("browserSources")).toObject();
	for (auto it = browserSources.begin(); it != browserSources.end(); ++it) {
		const QUrl url(it.value().toString());
		if (url.isValid() && url.scheme() == QStringLiteral("https"))
			browserSourceUrls_.insert(it.key(), url);
	}
	if (sourceStatus_) {
		sourceStatus_->setText(browserSourceUrls_.isEmpty()
			? QStringLiteral("The server did not provide Browser Source URLs.")
			: QStringLiteral("Browser Source URLs loaded. Existing Bacons Helper sources were synchronized."));
	}
	syncExistingBrowserSources();

	dashboardLinks_.clear();
	const QJsonObject links = payload.value(QStringLiteral("links")).toObject();
	for (auto it = links.begin(); it != links.end(); ++it) {
		const QUrl url(it.value().toString());
		if (url.isValid() && url.scheme() == QStringLiteral("https"))
			dashboardLinks_.insert(it.key(), url);
	}
	applyingState_ = false;
}

void BaconsHelperDock::saveChannelToggle(const QString &key, bool checked)
{
	if (token_.isEmpty())
		return;
	QJsonObject payload{{QStringLiteral("settings"), QJsonObject{{key, checked}}}};
	auto *reply = sendJson("PATCH", QStringLiteral("/api/obs-plugin/settings"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty()) {
				setStatus(QStringLiteral("⚠ Could not save %1; refreshing its server value.").arg(key), false);
				refresh();
			}
		} else {
			setStatus(QStringLiteral("✓ Channel setting saved."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::saveStreamEvent(const QString &key, bool checked)
{
	if (token_.isEmpty())
		return;
	QJsonObject payload{{QStringLiteral("events"), QJsonObject{{key, checked}}}};
	auto *reply = sendJson("PUT", QStringLiteral("/api/obs-plugin/stream-events"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty()) {
				setStatus(QStringLiteral("⚠ Stream Event setting was not saved; refreshing."), false);
				refresh();
			}
		} else {
			setStatus(QStringLiteral("✓ Stream Event setting saved."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::testStreamEvent()
{
	if (token_.isEmpty())
		return;
	const QString type = testEventType_->currentData().toString();
	testEventButton_->setEnabled(false);
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/stream-events/test"),
						   QJsonObject{{QStringLiteral("type"), type}});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		testEventButton_->setEnabled(!token_.isEmpty());
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Stream Event test could not be sent."), false);
		} else {
			const QJsonObject response = QJsonDocument::fromJson(bytes).object();
			const int delivered = response.value(QStringLiteral("delivered")).toInt();
			setStatus(QStringLiteral("✓ Test event sent — %1 local overlay client(s) received it.").arg(delivered), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::installBrowserSource(const QString &key)
{
	const QUrl url = browserSourceUrls_.value(key);
	if (!url.isValid() || url.scheme() != QStringLiteral("https")) {
		setStatus(QStringLiteral("⚠ Refresh Bacons Helper before installing this Browser Source."), false);
		return;
	}

	const QMap<QString, QString> sourceNames = {
		{QStringLiteral("streamEvents"), QStringLiteral("BH Stream Events")},
		{QStringLiteral("countdown"), QStringLiteral("BH Countdown")},
		{QStringLiteral("winLoss"), QStringLiteral("BH Win Loss")},
		{QStringLiteral("deathCounter"), QStringLiteral("BH Death Counter")},
		{QStringLiteral("shotCounter"), QStringLiteral("BH Shot Counter")},
		{QStringLiteral("credits"), QStringLiteral("BH Credits")},
	};
	const QString sourceName = sourceNames.value(key);
	if (sourceName.isEmpty())
		return;
	const QByteArray sourceNameUtf8 = sourceName.toUtf8();
	const QByteArray urlUtf8 = url.toString(QUrl::FullyEncoded).toUtf8();
	const bool compact = key == QStringLiteral("winLoss") || key == QStringLiteral("deathCounter") || key == QStringLiteral("shotCounter");
	const int width = compact ? 900 : 1920;
	const int height = compact ? 360 : 1080;

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene) {
		if (sceneSource) obs_source_release(sceneSource);
		setStatus(QStringLiteral("⚠ OBS has no current scene to receive the Browser Source."), false);
		return;
	}

	obs_source_t *source = obs_get_source_by_name(sourceNameUtf8.constData());
	if (source && std::strcmp(obs_source_get_id(source), "browser_source") != 0) {
		obs_source_release(source);
		obs_source_release(sceneSource);
		setStatus(QStringLiteral("⚠ A non-Browser Source named '%1' already exists. Rename it first.").arg(sourceName), false);
		return;
	}

	if (source) {
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "url", urlUtf8.constData());
		obs_data_set_int(settings, "width", width);
		obs_data_set_int(settings, "height", height);
		obs_source_update(source, settings);
		obs_data_release(settings);
	} else {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", urlUtf8.constData());
		obs_data_set_int(settings, "width", width);
		obs_data_set_int(settings, "height", height);
		source = obs_source_create("browser_source", sourceNameUtf8.constData(), settings, nullptr);
		obs_data_release(settings);
		if (!source) {
			obs_source_release(sceneSource);
			setStatus(QStringLiteral("⚠ OBS Browser Source support is unavailable."), false);
			return;
		}
	}

	if (!obs_scene_find_source(scene, sourceNameUtf8.constData()))
		obs_scene_add(scene, source);
	obs_source_release(source);
	obs_source_release(sceneSource);

	if (sourceStatus_)
		sourceStatus_->setText(QStringLiteral("%1 is installed and synchronized in the current scene.").arg(sourceName));
	setStatus(QStringLiteral("✓ %1 Browser Source installed / updated.").arg(sourceName), true);
}

void BaconsHelperDock::syncExistingBrowserSources()
{
	const QMap<QString, QString> sourceNames = {
		{QStringLiteral("streamEvents"), QStringLiteral("BH Stream Events")},
		{QStringLiteral("countdown"), QStringLiteral("BH Countdown")},
		{QStringLiteral("winLoss"), QStringLiteral("BH Win Loss")},
		{QStringLiteral("deathCounter"), QStringLiteral("BH Death Counter")},
		{QStringLiteral("shotCounter"), QStringLiteral("BH Shot Counter")},
		{QStringLiteral("credits"), QStringLiteral("BH Credits")},
	};
	for (auto it = browserSourceUrls_.cbegin(); it != browserSourceUrls_.cend(); ++it) {
		const QString sourceName = sourceNames.value(it.key());
		if (sourceName.isEmpty() || !it.value().isValid())
			continue;
		const QByteArray sourceNameUtf8 = sourceName.toUtf8();
		obs_source_t *source = obs_get_source_by_name(sourceNameUtf8.constData());
		if (!source)
			continue;
		if (std::strcmp(obs_source_get_id(source), "browser_source") == 0) {
			obs_data_t *settings = obs_source_get_settings(source);
			const QByteArray urlUtf8 = it.value().toString(QUrl::FullyEncoded).toUtf8();
			obs_data_set_string(settings, "url", urlUtf8.constData());
			obs_source_update(source, settings);
			obs_data_release(settings);
		}
		obs_source_release(source);
	}
}

void BaconsHelperDock::updateDeathCounterSelection()
{
	const QString counterId = deathCounterSelect_ ? deathCounterSelect_->currentData().toString() : QString{};
	const bool available = !token_.isEmpty() && deathCounters_.contains(counterId);
	for (QWidget *control : {static_cast<QWidget *>(deathCounterMinusButton_), static_cast<QWidget *>(deathCounterPlusButton_),
						 static_cast<QWidget *>(deathCounterResetButton_), static_cast<QWidget *>(deathCounterSetValue_),
						 static_cast<QWidget *>(deathCounterSetButton_)})
		control->setEnabled(available);
	if (!available) {
		if (deathCounterValue_)
			deathCounterValue_->setText(deathCounters_.isEmpty()
				? QStringLiteral("No saved death counters were found for this channel.")
				: QStringLiteral("Select a death counter."));
		return;
	}
	const QJsonObject counter = deathCounters_.value(counterId);
	const int total = counter.value(QStringLiteral("total")).toInt();
	deathCounterSetValue_->setValue(total);
	deathCounterValue_->setText(QStringLiteral("%1 — %2 death%3")
		.arg(counter.value(QStringLiteral("gameName")).toString(QStringLiteral("Unknown game")))
		.arg(total)
		.arg(total == 1 ? QString{} : QStringLiteral("s")));
}

void BaconsHelperDock::runDeathCounterAction(const QString &action, int value)
{
	if (token_.isEmpty())
		return;
	const QString counterId = deathCounterSelect_->currentData().toString();
	if (counterId.isEmpty() || !deathCounters_.contains(counterId)) {
		setStatus(QStringLiteral("⚠ Select a death counter first."), false);
		return;
	}
	QJsonObject payload{{QStringLiteral("action"), action}};
	if (action == QStringLiteral("set"))
		payload.insert(QStringLiteral("value"), value);
	const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(counterId));
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/death-counters/%1").arg(encodedId), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply, counterId] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Death counter update failed: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
			reply->deleteLater();
			return;
		}
		const QJsonObject counter = response.value(QStringLiteral("counter")).toObject();
		deathCounters_.insert(counterId, counter);
		const int index = deathCounterSelect_->findData(counterId);
		if (index >= 0) {
			deathCounterSelect_->setItemText(index, QStringLiteral("%1 — %2")
				.arg(counter.value(QStringLiteral("gameName")).toString(QStringLiteral("Unknown game")))
				.arg(counter.value(QStringLiteral("total")).toInt()));
		}
		updateDeathCounterSelection();
		setStatus(QStringLiteral("✓ Death counter updated."), true);
		reply->deleteLater();
	});
}

void BaconsHelperDock::applyLoyaltyState(const QJsonObject &loyalty)
{
	const bool previousApplying = applyingState_;
	applyingState_ = true;
	loyaltyState_ = loyalty;
	const QJsonObject settings = loyalty.value(QStringLiteral("settings")).toObject();
	loyaltyEnabled_->setChecked(settings.value(QStringLiteral("active")).toBool());
	loyaltyCurrency_->setText(settings.value(QStringLiteral("currencyName")).toString(QStringLiteral("Points")));
	loyaltyAmount_->setValue(settings.value(QStringLiteral("amount")).toInt());
	loyaltyInterval_->setValue(settings.value(QStringLiteral("timerInterval")).toInt(5));
	loyaltyTicketCost_->setValue(settings.value(QStringLiteral("ticketCost")).toInt(1));
	loyaltyMaxTickets_->setValue(settings.value(QStringLiteral("maxTickets")).toInt());
	loyaltyRafflePoints_->setValue(settings.value(QStringLiteral("rafflePoints")).toInt(100));
	loyaltyRaffleDuration_->setValue(settings.value(QStringLiteral("raffleDuration")).toInt(60));
	const QJsonObject summary = loyalty.value(QStringLiteral("summary")).toObject();
	loyaltySummary_->setText(QStringLiteral("%1 users • %2 total %3 • %4 active giveaway participant%5 • %6 ticket%7")
		.arg(summary.value(QStringLiteral("loyaltyUsers")).toInt())
		.arg(summary.value(QStringLiteral("loyaltyPoints")).toDouble(), 0, 'f', 0)
		.arg(settings.value(QStringLiteral("currencyName")).toString(QStringLiteral("Points")))
		.arg(summary.value(QStringLiteral("participants")).toInt())
		.arg(summary.value(QStringLiteral("participants")).toInt() == 1 ? QString{} : QStringLiteral("s"))
		.arg(summary.value(QStringLiteral("tickets")).toDouble(), 0, 'f', 0)
		.arg(summary.value(QStringLiteral("tickets")).toInt() == 1 ? QString{} : QStringLiteral("s")));

	const bool hasGiveaway = !settings.value(QStringLiteral("giveawayId")).isNull() &&
		!settings.value(QStringLiteral("giveawayId")).toString().isEmpty();
	const bool giveawayActive = settings.value(QStringLiteral("giveawayActive")).toBool();
	const QJsonObject raffle = loyalty.value(QStringLiteral("raffle")).toObject();
	const bool raffleRunning = raffle.value(QStringLiteral("running")).toBool();
	QString giveawayText = hasGiveaway
		? QStringLiteral("Giveaway: %1 — %2").arg(settings.value(QStringLiteral("giveawayName")).toString(QStringLiteral("Untitled")),
			giveawayActive ? QStringLiteral("active") : QStringLiteral("paused"))
		: QStringLiteral("No giveaway is configured.");
	if (raffleRunning)
		giveawayText += QStringLiteral(" • Raffle running for %1 %2")
			.arg(raffle.value(QStringLiteral("points")).toInt())
			.arg(settings.value(QStringLiteral("currencyName")).toString(QStringLiteral("Points")));
	const QJsonArray prizes = loyalty.value(QStringLiteral("prizes")).toArray();
	if (!prizes.isEmpty())
		giveawayText += QStringLiteral(" • %1 prize%2 configured").arg(prizes.size()).arg(prizes.size() == 1 ? QString{} : QStringLiteral("s"));
	giveawayStatus_->setText(giveawayText);
	if (hasGiveaway)
		giveawayName_->setText(settings.value(QStringLiteral("giveawayName")).toString());

	const bool connected = !token_.isEmpty();
	const bool loyaltyActive = settings.value(QStringLiteral("active")).toBool();
	loyaltyEnabled_->setEnabled(connected && (botIsModerator_ || loyaltyActive));
	giveawayStartButton_->setEnabled(connected && loyaltyActive && !hasGiveaway);
	giveawayPauseButton_->setEnabled(connected && hasGiveaway && giveawayActive);
	giveawayResumeButton_->setEnabled(connected && hasGiveaway && !giveawayActive && loyaltyActive);
	giveawayEndButton_->setEnabled(connected && hasGiveaway);
	giveawayPickButton_->setEnabled(connected && hasGiveaway);
	raffleStartButton_->setEnabled(connected && hasGiveaway && giveawayActive && loyaltyActive && !raffleRunning);
	raffleCancelButton_->setEnabled(connected && raffleRunning);
	applyingState_ = previousApplying;
}

void BaconsHelperDock::saveLoyaltySettings()
{
	if (token_.isEmpty())
		return;
	QJsonObject settings{
		{QStringLiteral("currency_name"), loyaltyCurrency_->text().trimmed()},
		{QStringLiteral("amount"), loyaltyAmount_->value()},
		{QStringLiteral("timer_interval"), loyaltyInterval_->value()},
		{QStringLiteral("ticket_cost"), loyaltyTicketCost_->value()},
		{QStringLiteral("max_tickets"), loyaltyMaxTickets_->value()},
		{QStringLiteral("raffle_points"), loyaltyRafflePoints_->value()},
		{QStringLiteral("raffle_duration"), loyaltyRaffleDuration_->value()}};
	auto *reply = sendJson("PUT", QStringLiteral("/api/obs-plugin/loyalty/settings"),
		QJsonObject{{QStringLiteral("settings"), settings}});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Loyalty settings were not saved: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
		} else {
			applyLoyaltyState(response.value(QStringLiteral("loyalty")).toObject());
			setStatus(QStringLiteral("✓ Loyalty settings saved."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::setLoyaltyActive(bool enabled)
{
	if (token_.isEmpty())
		return;
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/loyalty/active"),
		QJsonObject{{QStringLiteral("enabled"), enabled}});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty()) {
				setStatus(QStringLiteral("⚠ Loyalty Points state was not changed: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
				applyLoyaltyState(loyaltyState_);
			}
		} else {
			applyLoyaltyState(response.value(QStringLiteral("loyalty")).toObject());
			setStatus(QStringLiteral("✓ Loyalty Points state updated."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::runGiveawayAction(const QString &action)
{
	if (token_.isEmpty())
		return;
	QJsonObject payload{{QStringLiteral("action"), action}};
	if (action == QStringLiteral("start"))
		payload.insert(QStringLiteral("name"), giveawayName_->text().trimmed());
	else if (action == QStringLiteral("pick-winners"))
		payload.insert(QStringLiteral("count"), giveawayWinnerCount_->value());
	else if (action == QStringLiteral("start-raffle")) {
		payload.insert(QStringLiteral("points"), loyaltyRafflePoints_->value());
		payload.insert(QStringLiteral("duration"), loyaltyRaffleDuration_->value());
	}
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/giveaway/action"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply, action] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Giveaway action failed: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
			reply->deleteLater();
			return;
		}
		applyLoyaltyState(response.value(QStringLiteral("loyalty")).toObject());
		const QJsonArray winners = response.value(QStringLiteral("winners")).toArray();
		if (!winners.isEmpty()) {
			QStringList names;
			for (const auto &value : winners) {
				const QJsonObject winner = value.toObject();
				names << winner.value(QStringLiteral("user_name")).toString(
					winner.value(QStringLiteral("user_login")).toString(QStringLiteral("Unknown")));
			}
			setStatus(QStringLiteral("✓ Winner%1: %2").arg(winners.size() == 1 ? QString{} : QStringLiteral("s"), names.join(QStringLiteral(", "))), true);
		} else {
			setStatus(QStringLiteral("✓ Giveaway action completed: %1").arg(action), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::updateWalkOnSelection()
{
	const QString userId = walkOnSelect_ ? walkOnSelect_->currentData().toString() : QString{};
	const bool available = !token_.isEmpty() && walkOns_.contains(userId);
	for (QWidget *control : {static_cast<QWidget *>(walkOnEnabled_), static_cast<QWidget *>(walkOnOverlay_),
						 static_cast<QWidget *>(walkOnColor_), static_cast<QWidget *>(walkOnSaveButton_),
						 static_cast<QWidget *>(walkOnTestButton_)})
		control->setEnabled(available);
	walkOnShoutout_->setEnabled(available && botIsModerator_);
	walkOnAnnouncement_->setEnabled(available && botIsModerator_);
	if (!available) {
		walkOnSummary_->setText(walkOns_.isEmpty()
			? QStringLiteral("No Walk-On users are configured for this channel.")
			: QStringLiteral("Select a Walk-On user."));
		return;
	}
	const bool previousApplying = applyingState_;
	applyingState_ = true;
	const QJsonObject walkOn = walkOns_.value(userId);
	walkOnEnabled_->setChecked(walkOn.value(QStringLiteral("enabled")).toBool(true));
	walkOnOverlay_->setChecked(walkOn.value(QStringLiteral("overlayEnabled")).toBool(true));
	walkOnShoutout_->setChecked(walkOn.value(QStringLiteral("shoutout")).toBool());
	walkOnAnnouncement_->setChecked(walkOn.value(QStringLiteral("announcement")).toBool());
	const int colorIndex = walkOnColor_->findData(walkOn.value(QStringLiteral("announcementColor")).toString(QStringLiteral("primary")));
	walkOnColor_->setCurrentIndex(colorIndex >= 0 ? colorIndex : 0);
	QString summary = QStringLiteral("@%1 • %2 message%3")
		.arg(walkOn.value(QStringLiteral("login")).toString())
		.arg(walkOn.value(QStringLiteral("messageCount")).toInt())
		.arg(walkOn.value(QStringLiteral("messageCount")).toInt() == 1 ? QString{} : QStringLiteral("s"));
	if (walkOn.value(QStringLiteral("audioConfigured")).toBool())
		summary += QStringLiteral(" • audio configured");
	const QString preview = walkOn.value(QStringLiteral("previewMessage")).toString();
	if (!preview.isEmpty())
		summary += QStringLiteral("\nPreview: %1").arg(preview);
	if (!botIsModerator_)
		summary += QStringLiteral("\nModerator status is required to enable shoutouts or announcements.");
	walkOnSummary_->setText(summary);
	applyingState_ = previousApplying;
}

void BaconsHelperDock::saveWalkOnSettings()
{
	if (token_.isEmpty())
		return;
	const QString userId = walkOnSelect_->currentData().toString();
	if (userId.isEmpty() || !walkOns_.contains(userId)) {
		setStatus(QStringLiteral("⚠ Select a Walk-On user first."), false);
		return;
	}
	QJsonObject settings{
		{QStringLiteral("enabled"), walkOnEnabled_->isChecked()},
		{QStringLiteral("overlayEnabled"), walkOnOverlay_->isChecked()},
		{QStringLiteral("shoutout"), walkOnShoutout_->isChecked()},
		{QStringLiteral("announcement"), walkOnAnnouncement_->isChecked()},
		{QStringLiteral("announcementColor"), walkOnColor_->currentData().toString()}};
	const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(userId));
	auto *reply = sendJson("PUT", QStringLiteral("/api/obs-plugin/walk-ons/%1").arg(encodedId),
		QJsonObject{{QStringLiteral("settings"), settings}});
	connect(reply, &QNetworkReply::finished, this, [this, reply, userId] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Walk-On settings were not saved: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
			reply->deleteLater();
			return;
		}
		walkOns_.clear();
		const QString selectedId = userId;
		walkOnSelect_->clear();
		for (const auto &value : response.value(QStringLiteral("walkOns")).toArray()) {
			const QJsonObject walkOn = value.toObject();
			const QString id = walkOn.value(QStringLiteral("userId")).toString();
			if (id.isEmpty())
				continue;
			walkOns_.insert(id, walkOn);
			walkOnSelect_->addItem(walkOn.value(QStringLiteral("displayName")).toString(
				walkOn.value(QStringLiteral("login")).toString(id)), id);
		}
		const int index = walkOnSelect_->findData(selectedId);
		if (index >= 0)
			walkOnSelect_->setCurrentIndex(index);
		updateWalkOnSelection();
		setStatus(QStringLiteral("✓ Walk-On settings saved."), true);
		reply->deleteLater();
	});
}

void BaconsHelperDock::testWalkOn()
{
	if (token_.isEmpty())
		return;
	const QString userId = walkOnSelect_->currentData().toString();
	if (userId.isEmpty() || !walkOns_.contains(userId)) {
		setStatus(QStringLiteral("⚠ Select a Walk-On user first."), false);
		return;
	}
	const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(userId));
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/walk-ons/%1/test").arg(encodedId));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		const QJsonObject response = QJsonDocument::fromJson(bytes).object();
		if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("ok")).toBool()) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Walk-On preview failed: %1")
					.arg(response.value(QStringLiteral("error")).toString(reply->errorString())), false);
		} else {
			setStatus(QStringLiteral("✓ Walk-On preview sent to %1 local overlay client(s).")
				.arg(response.value(QStringLiteral("delivered")).toInt()), true);
		}
		reply->deleteLater();
	});
}


void BaconsHelperDock::saveOverlaySettings(const QString &key)
{
	if (token_.isEmpty())
		return;

	QJsonObject settings = overlaySettings_.value(key);
	if (key == QStringLiteral("winloss")) {
		settings.insert(QStringLiteral("color"), winLossColor_->text());
		settings.insert(QStringLiteral("size"), QString::number(winLossSize_->value()));
		settings.insert(QStringLiteral("show_game_title"), winLossGameTitle_->isChecked());
		settings.insert(QStringLiteral("show_ratio"), winLossRatio_->isChecked());
		settings.insert(QStringLiteral("show_win_percent"), winLossWinPercent_->isChecked());
		settings.insert(QStringLiteral("show_loss_percent"), winLossLossPercent_->isChecked());
		settings.insert(QStringLiteral("show_last_game"), winLossLastGame_->isChecked());
	} else if (key == QStringLiteral("deathcounter")) {
		settings.insert(QStringLiteral("color"), deathColor_->text());
		settings.insert(QStringLiteral("size"), QString::number(deathSize_->value()));
		settings.insert(QStringLiteral("show_title"), deathShowTitle_->isChecked());
		settings.insert(QStringLiteral("use_custom_text"), deathUseCustomText_->isChecked());
		settings.insert(QStringLiteral("custom_text"), deathCustomText_->text());
	} else if (key == QStringLiteral("shotcounter")) {
		settings.insert(QStringLiteral("color"), shotColor_->text());
		settings.insert(QStringLiteral("size"), QString::number(shotSize_->value()));
		settings.insert(QStringLiteral("custom_text"), shotCustomText_->text());
	} else if (key == QStringLiteral("credits")) {
		settings.insert(QStringLiteral("titlesMode"), creditsTitlesMode_->currentData().toString());
		settings.insert(QStringLiteral("showAvgViewers"), creditsShowAvg_->isChecked());
		settings.insert(QStringLiteral("showBoxArt"), creditsShowBoxArt_->isChecked());
		settings.insert(QStringLiteral("scrollSpeedPx"), creditsScrollSpeed_->value());
		settings.insert(QStringLiteral("bannerText"), creditsBannerText_->text());
		settings.insert(QStringLiteral("bannerPos"), creditsBannerPosition_->currentData().toString());
	} else {
		return;
	}

	auto *reply = sendJson("PUT", QStringLiteral("/api/obs-plugin/overlays/%1").arg(key),
						   QJsonObject{{QStringLiteral("settings"), settings}});
	connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
		const QByteArray bytes = reply->readAll();
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty()) {
				setStatus(QStringLiteral("⚠ Overlay setting was not saved; refreshing."), false);
				refresh();
			}
		} else {
			const QJsonObject response = QJsonDocument::fromJson(bytes).object();
			if (response.value(QStringLiteral("ok")).toBool()) {
				overlaySettings_.insert(key, response.value(QStringLiteral("settings")).toObject());
				setStatus(QStringLiteral("✓ %1 overlay settings saved.").arg(key), true);
				refresh();
			} else {
				setStatus(QStringLiteral("⚠ Bacons Helper rejected the overlay setting."), false);
			}
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::updateCountdownStatus()
{
	if (countdownEndTime_ <= 0) {
		countdownStatus_->setText(QStringLiteral("Countdown: inactive"));
		return;
	}
	const qint64 remainingMs = countdownEndTime_ - QDateTime::currentMSecsSinceEpoch();
	if (remainingMs <= 0) {
		countdownEndTime_ = 0;
		countdownStatus_->setText(QStringLiteral("Countdown: complete"));
		return;
	}
	const qint64 remaining = qMax<qint64>(1, (remainingMs + 999) / 1000);
	const qint64 minutes = remaining / 60;
	const qint64 seconds = remaining % 60;
	if (minutes > 0) {
		countdownStatus_->setText(QStringLiteral("Countdown: %1:%2 remaining")
							 .arg(minutes)
							 .arg(seconds, 2, 10, QLatin1Char('0')));
	} else {
		countdownStatus_->setText(QStringLiteral("Countdown: %1 seconds remaining").arg(seconds));
	}
}

void BaconsHelperDock::startCountdown()
{
	if (token_.isEmpty())
		return;
	QJsonObject payload{{QStringLiteral("durationSeconds"), countdownSeconds_->value()}};
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/countdown"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray bytes = reply->readAll();
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Countdown could not be started."), false);
		} else {
			const QJsonObject response = QJsonDocument::fromJson(bytes).object();
			countdownEndTime_ = qRound64(response.value(QStringLiteral("endTime")).toDouble());
			if (countdownEndTime_ <= QDateTime::currentMSecsSinceEpoch())
				countdownEndTime_ = QDateTime::currentMSecsSinceEpoch() + (countdownSeconds_->value() * 1000LL);
			updateCountdownStatus();
			setStatus(QStringLiteral("✓ Countdown started."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::startCountdownPreset(int seconds)
{
	countdownSeconds_->setValue(seconds);
	startCountdown();
}

void BaconsHelperDock::cancelCountdown()
{
	if (token_.isEmpty())
		return;
	auto *reply = sendJson("DELETE", QStringLiteral("/api/obs-plugin/countdown"));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			if (!token_.isEmpty())
				setStatus(QStringLiteral("⚠ Countdown could not be cancelled."), false);
		} else {
			countdownEndTime_ = 0;
			updateCountdownStatus();
			setStatus(QStringLiteral("✓ Countdown cancelled."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::openDashboardLink(const QString &key)
{
	const QUrl url = dashboardLinks_.value(key);
	if (!url.isValid()) {
		setStatus(QStringLiteral("⚠ Refresh first so the dashboard link can be loaded safely."), false);
		return;
	}
	QDesktopServices::openUrl(url);
}
