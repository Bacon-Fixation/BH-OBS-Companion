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
#include <QDir>
#include <QFileInfo>
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
#include <QSettings>
#include <QSpinBox>
#include <QSslSocket>
#include <QStringList>
#include <QSysInfo>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
constexpr auto kDefaultServer = "https://baconshelper.com";

bool isHttpsUrl(const QUrl &url)
{
	return url.isValid() && url.scheme() == QStringLiteral("https") &&
	       !url.host().isEmpty() && url.userInfo().isEmpty();
}

const QMap<QString, QString> kBrowserSourceNames = {
	{QStringLiteral("streamEvents"), QStringLiteral("BH Stream Events")},
	{QStringLiteral("countdown"), QStringLiteral("BH Countdown")},
	{QStringLiteral("winLoss"), QStringLiteral("BH Win Loss")},
	{QStringLiteral("deathCounter"), QStringLiteral("BH Death Counter")},
	{QStringLiteral("shotCounter"), QStringLiteral("BH Shot Counter")},
	{QStringLiteral("credits"), QStringLiteral("BH Credits")},
};

QString legacyOverlayChannel(const QUrl &url)
{
	const auto encoded = QUrlQuery(url).queryItemValue(QStringLiteral("p"), QUrl::FullyDecoded).toLatin1();
	const auto json = QJsonDocument::fromJson(QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding));
	return json.object().value(QStringLiteral("username")).toString();
}

void applyOverlayText(QLineEdit *edit, const QString &value)
{
	if (!edit->hasFocus() || !edit->isModified())
		edit->setText(value);
}

int overlaySize(const QJsonObject &settings)
{
	const auto size = settings.value(QStringLiteral("size"));
	if (size.isDouble())
		return size.toInt(64);
	bool valid = false;
	const int result = size.toString().toInt(&valid);
	return valid ? result : 64;
}

QString configPath()
{
	if (!obs_current_module())
		return {};
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

bool BaconsHelperDock::sameOverlay(const QUrl &current, const QUrl &updated)
{
	if (!isHttpsUrl(current) || !isHttpsUrl(updated) ||
	    current.host() != updated.host() || current.port(443) != updated.port(443) ||
	    current.path() != updated.path())
		return false;
	if (updated.path().startsWith(QStringLiteral("/overlays/")))
		return true; // This URL's path identifies both the channel and the overlay.
	const auto channel = legacyOverlayChannel(updated);
	return !channel.isEmpty() && legacyOverlayChannel(current) == channel;
}

BaconsHelperDock::BaconsHelperDock(QWidget *parent) : QWidget(parent)
{
#ifdef _WIN32
	// Qt 6 implements TLS through runtime plugins. OBS already owns the Qt
	// runtime. Add this module's private Qt plugin tree before probing TLS so we
	// do not have to install or overwrite plugins in OBS's global Qt directory.
	char *qtPluginPath = obs_current_module() ? obs_module_file("qt-plugins") : nullptr;
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
	setControlsEnabled(false);
	if (!token_.isEmpty()) {
		refresh();
	} else if (!credentialStorageWarning_.isEmpty()) {
		setStatus(QStringLiteral("⚠ Not paired — %1")
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
	status_->setTextFormat(Qt::PlainText);
	channel_ = new QLabel(QStringLiteral("Channel: not paired"));
	channel_->setTextFormat(Qt::PlainText);
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
	disconnectButton_ = new QPushButton(QStringLiteral("Forget this pairing"));
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
		spin->setKeyboardTracking(false);
		connect(spin, &QSpinBox::valueChanged, this, [this, key](int) {
			if (!applyingState_) saveOverlaySettings(key);
		});
	};
	const auto saveOverlayOnEdit = [this](QLineEdit *edit, const QString &key) {
		connect(edit, &QLineEdit::editingFinished, this, [this, key, edit] {
			if (!applyingState_ && edit->isModified() && edit->hasAcceptableInput()) {
				saveOverlaySettings(key);
				edit->setModified(false);
			}
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
	const QString path = configPath();
	if (path.isEmpty()) {
		credentialStorageWarning_ = QStringLiteral("The OBS configuration path is unavailable.");
		return;
	}
	QSettings settings(path, QSettings::IniFormat);
	const QString server = settings.value(QStringLiteral("server"), QString::fromLatin1(kDefaultServer)).toString();
	serverUrl_->setText(server);
	const QString stored = settings.value(QStringLiteral("credential")).toString();
	const bool reversible = stored.startsWith(QStringLiteral("scoped:"));
	if (!isHttpsUrl(QUrl(server, QUrl::StrictMode))) {
		if (reversible) {
			settings.remove(QStringLiteral("credential"));
			settings.sync();
		}
		credentialStorageWarning_ = QStringLiteral("The saved server address is invalid.");
		return;
	}

#ifdef BH_HAVE_LIBSECRET
	QString lookupError;
	if (stored == QStringLiteral("secret-service") || reversible)
		token_ = secretServiceLookup(server, &lookupError);
	if (!lookupError.isEmpty())
		credentialStorageWarning_ = lookupError;

	// Migrate credentials written by the early Linux prototype. "scoped:" was
	// only Base64 and therefore was not encrypted at rest.
	if (reversible) {
		const QString migrated = QString::fromUtf8(QByteArray::fromBase64(stored.mid(7).toLatin1()));
		// Never leave the reversible legacy value on disk, even when a Secret
		// Service entry already exists or the legacy value is malformed.
		if (token_.isEmpty())
			settings.remove(QStringLiteral("credential"));
		else
			settings.setValue(QStringLiteral("credential"), QStringLiteral("secret-service"));
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
	// Session-only builds and Windows cannot migrate the older Linux credential.
	if (reversible) {
		settings.remove(QStringLiteral("credential"));
		settings.sync();
	}
#ifdef _WIN32
	token_ = unprotectToken(stored);
#endif
#endif
}

bool BaconsHelperDock::saveLocalSettings(QString *credentialError)
{
	const QString path = configPath();
	if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (credentialError)
			*credentialError = QStringLiteral("The OBS configuration directory is unavailable.");
		return false;
	}
	QSettings settings(path, QSettings::IniFormat);
#ifdef BH_HAVE_LIBSECRET
	const QString previousServer = settings.value(QStringLiteral("server"), QString::fromLatin1(kDefaultServer)).toString();
#endif
	const QString server = serverUrl_->text().trimmed();
	settings.setValue(QStringLiteral("server"), server);
	const auto syncSettings = [&settings, credentialError] {
		settings.sync();
		if (settings.status() == QSettings::NoError)
			return true;
		if (credentialError)
			*credentialError = QStringLiteral("The OBS configuration file could not be written.");
		return false;
	};

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
	return syncSettings() && credentialSaved;
#else
	const QString protectedToken = protectToken(token_);
	if (!token_.isEmpty() && protectedToken.isEmpty()) {
		settings.remove(QStringLiteral("credential"));
		if (syncSettings() && credentialError) {
#ifdef _WIN32
			*credentialError = QStringLiteral("The operating system credential store was unavailable.");
#else
			*credentialError = QStringLiteral("This build keeps the pairing for the current OBS session.");
#endif
		}
		return false;
	}
	if (protectedToken.isEmpty())
		settings.remove(QStringLiteral("credential"));
	else
		settings.setValue(QStringLiteral("credential"), protectedToken);
	return syncSettings();
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
	ready_ = enabled;
	const bool canPair = !pairing_ && token_.isEmpty();
	serverUrl_->setEnabled(canPair);
	pairCode_->setEnabled(canPair);
	pairButton_->setEnabled(canPair);

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
	refreshButton_->setEnabled(!token_.isEmpty());
	disconnectButton_->setEnabled(!token_.isEmpty());
}

bool BaconsHelperDock::validateServerUrl()
{
	QUrl url(serverUrl_->text().trimmed(), QUrl::StrictMode);
	if (!isHttpsUrl(url)) {
		setStatus(QStringLiteral("⚠ Server URL must be a valid HTTPS address."), false);
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

void BaconsHelperDock::sendJson(const QByteArray &method, const QString &path,
							   const QJsonObject &payload, JsonCallback callback)
{
	if (method != "GET")
		++editRevision_;
	Request next{method, path, payload, std::move(callback)};
	if (path.startsWith(QStringLiteral("/api/obs-plugin/overlays/")) &&
	    !requests_.isEmpty() && requests_.back().path == path) {
		requests_.back() = std::move(next);
	} else {
		requests_.enqueue(std::move(next));
	}
	startNextRequest();
}

void BaconsHelperDock::startNextRequest()
{
	if (activeReply_ || requests_.isEmpty())
		return;

	const Request next = requests_.dequeue();
	QNetworkRequest request(endpoint(next.path));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("User-Agent", "Bacons-Helper-OBS/" BH_OBS_VERSION);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	request.setTransferTimeout(15000);
	if (!token_.isEmpty())
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());

	const QByteArray body = next.payload.isEmpty() ? QByteArray() :
		QJsonDocument(next.payload).toJson(QJsonDocument::Compact);
	auto *reply = next.method == "GET" ? network_->get(request) :
		network_->sendCustomRequest(request, next.method, body);
	activeReply_ = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply, callback = next.callback] {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto response = QJsonDocument::fromJson(reply->readAll()).object();
		const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 &&
			status < 300 && response.value(QStringLiteral("ok")).toBool();
		const QString error = response.value(QStringLiteral("error")).toString(
			reply->error() == QNetworkReply::NoError ? QStringLiteral("The server returned an unexpected response.") :
			reply->errorString());
		activeReply_ = nullptr;
		reply->deleteLater();

		if (status == 401 && !token_.isEmpty()) {
			clearSession();
			setStatus(QStringLiteral("✕ Pairing expired or was revoked. Pair this OBS installation again."), false);
			return;
		}
		callback(response, ok, status, error);
		startNextRequest();
	});
}

void BaconsHelperDock::clearSession()
{
	requests_.clear();
	refreshPending_ = false;
	if (auto *reply = activeReply_) {
		activeReply_ = nullptr;
		reply->disconnect(this);
		reply->abort();
		reply->deleteLater();
	}
	token_.clear();
	dashboardLinks_.clear();
	browserSourceUrls_.clear();
	overlaySettings_.clear();
	countdownEndTime_ = 0;
	updateCountdownStatus();
	sourceStatus_->setText(QStringLiteral("Pair again to manage Bacons Helper Browser Sources."));
	QString credentialError;
	credentialStorageWarning_ = saveLocalSettings(&credentialError) ? QString{} : credentialError;
	setControlsEnabled(false);
	channel_->setText(QStringLiteral("Channel: not paired"));
}

void BaconsHelperDock::pair()
{
	if (pairing_ || !token_.isEmpty() || !validateServerUrl())
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
	pairing_ = true;
	setControlsEnabled(false);
	sendJson("POST", QStringLiteral("/api/obs-plugin/link"), payload,
			 [this](const QJsonObject &response, bool ok, int status, const QString &error) {
		pairing_ = false;
		if (!ok) {
			setControlsEnabled(false);
			setStatus(status > 0 ? QStringLiteral("✕ Pairing failed (HTTP %1): %2").arg(status).arg(error) :
					  QStringLiteral("✕ Pairing failed: %1").arg(error), false);
			return;
		}

		const QString token = response.value(QStringLiteral("token")).toString();
		if (token.isEmpty() || token.contains(QLatin1Char('\r')) || token.contains(QLatin1Char('\n'))) {
			setControlsEnabled(false);
			setStatus(QStringLiteral("✕ Bacons Helper did not return a usable credential."), false);
			return;
		}
		token_ = token;
		pairCode_->clear();
		QString credentialError;
		credentialStorageWarning_ = saveLocalSettings(&credentialError) ? QString{} : credentialError;
		setControlsEnabled(false);
		refresh();
	});
}

void BaconsHelperDock::disconnectAccount()
{
	clearSession();
	const QString message = credentialStorageWarning_.isEmpty() ?
		QStringLiteral("Pairing forgotten on this computer. You can revoke the installation in the dashboard.") :
		QStringLiteral("Could not remove the saved pairing: %1. Revoke the installation in the dashboard.")
			.arg(credentialStorageWarning_);
	setStatus(message, false);
}

void BaconsHelperDock::refresh()
{
	if (refreshPending_ || token_.isEmpty() || !validateServerUrl())
		return;

	refreshPending_ = true;
	const quint64 revision = editRevision_;
	sendJson("GET", QStringLiteral("/api/obs-plugin/settings"), {},
			 [this, revision](const QJsonObject &response, bool ok, int, const QString &) {
		refreshPending_ = false;
		if (revision != editRevision_) {
			refresh();
			return;
		}
		if (!ok || !response.value(QStringLiteral("channel")).isObject() ||
		    !response.value(QStringLiteral("settings")).isObject()) {
			setStatus(QStringLiteral("⚠ Unable to refresh settings. Try refreshing again."), false);
			return;
		}
		setControlsEnabled(true);
		applySettingsPayload(response);
		if (!credentialStorageWarning_.isEmpty()) {
			setStatus(QStringLiteral("⚠ Connected for this OBS session: %1").arg(credentialStorageWarning_), true);
		} else {
			setStatus(QStringLiteral("✓ Connected — settings are synchronized with Bacons Helper."), true);
		}
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
		applyOverlayText(winLossColor_, winLoss.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		winLossSize_->setValue(overlaySize(winLoss));
		winLossGameTitle_->setChecked(winLoss.value(QStringLiteral("show_game_title")).toBool());
		winLossRatio_->setChecked(winLoss.value(QStringLiteral("show_ratio")).toBool());
		winLossWinPercent_->setChecked(winLoss.value(QStringLiteral("show_win_percent")).toBool());
		winLossLossPercent_->setChecked(winLoss.value(QStringLiteral("show_loss_percent")).toBool());
		winLossLastGame_->setChecked(winLoss.value(QStringLiteral("show_last_game")).toBool());
	}
	const QJsonObject deaths = readOverlay(QStringLiteral("deathcounter"));
	if (!deaths.isEmpty()) {
		applyOverlayText(deathColor_, deaths.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		deathSize_->setValue(overlaySize(deaths));
		deathShowTitle_->setChecked(deaths.value(QStringLiteral("show_title")).toBool());
		deathUseCustomText_->setChecked(deaths.value(QStringLiteral("use_custom_text")).toBool());
		applyOverlayText(deathCustomText_, deaths.value(QStringLiteral("custom_text")).toString(QStringLiteral("💀 Deaths")));
		deathCustomText_->setEnabled(deathUseCustomText_->isChecked());
	}
	const QJsonObject shots = readOverlay(QStringLiteral("shotcounter"));
	if (!shots.isEmpty()) {
		applyOverlayText(shotColor_, shots.value(QStringLiteral("color")).toString(QStringLiteral("#AAAAAA")));
		shotSize_->setValue(overlaySize(shots));
		applyOverlayText(shotCustomText_, shots.value(QStringLiteral("custom_text")).toString(QStringLiteral("Shots")));
	}
	const QJsonObject credits = readOverlay(QStringLiteral("credits"));
	if (!credits.isEmpty()) {
		const int titlesIndex = creditsTitlesMode_->findData(credits.value(QStringLiteral("titlesMode")).toString(QStringLiteral("latest")));
		creditsTitlesMode_->setCurrentIndex(titlesIndex >= 0 ? titlesIndex : 1);
		creditsShowAvg_->setChecked(credits.value(QStringLiteral("showAvgViewers")).toBool(true));
		creditsShowBoxArt_->setChecked(credits.value(QStringLiteral("showBoxArt")).toBool(true));
		creditsScrollSpeed_->setValue(credits.value(QStringLiteral("scrollSpeedPx")).toInt(40));
		applyOverlayText(creditsBannerText_, credits.value(QStringLiteral("bannerText")).toString());
		const int bannerIndex = creditsBannerPosition_->findData(credits.value(QStringLiteral("bannerPos")).toString(QStringLiteral("top")));
		creditsBannerPosition_->setCurrentIndex(bannerIndex >= 0 ? bannerIndex : 0);
	}

	const QJsonObject countdown = payload.value(QStringLiteral("countdown")).toObject();
	countdownEndTime_ = countdown.value(QStringLiteral("endTime")).toInteger();
	updateCountdownStatus();

	browserSourceUrls_.clear();
	const QJsonObject browserSources = payload.value(QStringLiteral("browserSources")).toObject();
	for (auto it = browserSources.begin(); it != browserSources.end(); ++it) {
		const QUrl url(it.value().toString());
		if (isHttpsUrl(url))
			browserSourceUrls_.insert(it.key(), url);
	}
	if (sourceStatus_) {
		sourceStatus_->setText(browserSourceUrls_.isEmpty()
			? QStringLiteral("The server did not provide Browser Source URLs.")
			: QStringLiteral("Browser Source URLs loaded."));
	}
	syncExistingBrowserSources();

	dashboardLinks_.clear();
	const QJsonObject links = payload.value(QStringLiteral("links")).toObject();
	for (auto it = links.begin(); it != links.end(); ++it) {
		const QUrl url(it.value().toString());
		if (isHttpsUrl(url))
			dashboardLinks_.insert(it.key(), url);
	}
	applyingState_ = false;
}

void BaconsHelperDock::saveChannelToggle(const QString &key, bool checked)
{
	if (!ready_)
		return;
	sendJson("PATCH", QStringLiteral("/api/obs-plugin/settings"),
			 QJsonObject{{QStringLiteral("settings"), QJsonObject{{key, checked}}}},
			 [this, key](const QJsonObject &, bool ok, int, const QString &) {
		setStatus(ok ? QStringLiteral("✓ Channel setting saved.") :
				  QStringLiteral("⚠ Could not save %1; refreshing its server value.").arg(key), ok);
		if (!ok) refresh();
	});
}

void BaconsHelperDock::saveStreamEvent(const QString &key, bool checked)
{
	if (!ready_)
		return;
	sendJson("PUT", QStringLiteral("/api/obs-plugin/stream-events"),
			 QJsonObject{{QStringLiteral("events"), QJsonObject{{key, checked}}}},
			 [this](const QJsonObject &, bool ok, int, const QString &) {
		setStatus(ok ? QStringLiteral("✓ Stream Event setting saved.") :
				  QStringLiteral("⚠ Stream Event setting was not saved; refreshing."), ok);
		if (!ok) refresh();
	});
}

void BaconsHelperDock::testStreamEvent()
{
	if (!ready_)
		return;
	const QString type = testEventType_->currentData().toString();
	testEventButton_->setEnabled(false);
	sendJson("POST", QStringLiteral("/api/obs-plugin/stream-events/test"),
			 QJsonObject{{QStringLiteral("type"), type}},
			 [this](const QJsonObject &response, bool ok, int, const QString &) {
		testEventButton_->setEnabled(ready_);
		const int delivered = response.value(QStringLiteral("delivered")).toInt();
		setStatus(ok ? QStringLiteral("✓ Test event sent — %1 local overlay client(s) received it.").arg(delivered) :
				  QStringLiteral("⚠ Stream Event test could not be sent."), ok);
	});
}

void BaconsHelperDock::installBrowserSource(const QString &key)
{
	const QUrl url = browserSourceUrls_.value(key);
	if (!isHttpsUrl(url)) {
		setStatus(QStringLiteral("⚠ Refresh Bacons Helper before installing this Browser Source."), false);
		return;
	}

	const QString sourceName = kBrowserSourceNames.value(key);
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
	if (source && std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
		obs_source_release(source);
		obs_source_release(sceneSource);
		setStatus(QStringLiteral("⚠ A non-Browser Source named '%1' already exists. Rename it first.").arg(sourceName), false);
		return;
	}

	if (source) {
		obs_data_t *settings = obs_source_get_settings(source);
		const auto current = QByteArray(obs_data_get_string(settings, "url"));
		if (!sameOverlay(QUrl::fromEncoded(current), url)) {
			obs_data_release(settings);
			obs_source_release(source);
			obs_source_release(sceneSource);
			setStatus(QStringLiteral("⚠ '%1' belongs to a different overlay or channel. Rename it to install this overlay.").arg(sourceName), false);
			return;
		}
		if (current != urlUtf8) {
			obs_data_set_string(settings, "url", urlUtf8.constData());
			obs_source_update(source, settings);
		}
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

	const bool installed = obs_scene_find_source_recursive(scene, sourceNameUtf8.constData()) ||
		obs_scene_add(scene, source);
	obs_source_release(source);
	obs_source_release(sceneSource);
	if (!installed) {
		setStatus(QStringLiteral("⚠ OBS could not add the Browser Source to this scene."), false);
		return;
	}

	if (sourceStatus_)
		sourceStatus_->setText(QStringLiteral("%1 is installed and synchronized in the current scene.").arg(sourceName));
	setStatus(QStringLiteral("✓ %1 Browser Source installed / updated.").arg(sourceName), true);
}

void BaconsHelperDock::syncExistingBrowserSources()
{
	for (auto it = browserSourceUrls_.cbegin(); it != browserSourceUrls_.cend(); ++it) {
		const QString sourceName = kBrowserSourceNames.value(it.key());
		if (sourceName.isEmpty() || !it.value().isValid())
			continue;
		const QByteArray sourceNameUtf8 = sourceName.toUtf8();
		obs_source_t *source = obs_get_source_by_name(sourceNameUtf8.constData());
		if (!source)
			continue;
		if (std::strcmp(obs_source_get_unversioned_id(source), "browser_source") == 0) {
			obs_data_t *settings = obs_source_get_settings(source);
			const QByteArray urlUtf8 = it.value().toString(QUrl::FullyEncoded).toUtf8();
			const auto current = QByteArray(obs_data_get_string(settings, "url"));
			if (current != urlUtf8 && sameOverlay(QUrl::fromEncoded(current), it.value())) {
				obs_data_set_string(settings, "url", urlUtf8.constData());
				obs_source_update(source, settings);
			}
			obs_data_release(settings);
		}
		obs_source_release(source);
	}
}

void BaconsHelperDock::saveOverlaySettings(const QString &key)
{
	if (!ready_)
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

	sendJson("PUT", QStringLiteral("/api/obs-plugin/overlays/%1").arg(key),
			 QJsonObject{{QStringLiteral("settings"), settings}},
			 [this, key](const QJsonObject &response, bool ok, int, const QString &) {
		if (ok && response.value(QStringLiteral("settings")).isObject())
			overlaySettings_.insert(key, response.value(QStringLiteral("settings")).toObject());
		setStatus(ok ? QStringLiteral("✓ %1 overlay settings saved.").arg(key) :
				  QStringLiteral("⚠ Overlay setting was not saved; refreshing."), ok);
		refresh();
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
	if (!ready_)
		return;
	const int duration = countdownSeconds_->value();
	sendJson("POST", QStringLiteral("/api/obs-plugin/countdown"),
			 QJsonObject{{QStringLiteral("durationSeconds"), duration}},
			 [this, duration](const QJsonObject &response, bool ok, int, const QString &) {
		if (ok) {
			countdownEndTime_ = response.value(QStringLiteral("endTime")).toInteger();
			if (countdownEndTime_ <= 0)
				countdownEndTime_ = QDateTime::currentMSecsSinceEpoch() + duration * 1000LL;
			updateCountdownStatus();
		}
		setStatus(ok ? QStringLiteral("✓ Countdown started.") :
				  QStringLiteral("⚠ Countdown could not be started."), ok);
	});
}

void BaconsHelperDock::startCountdownPreset(int seconds)
{
	countdownSeconds_->setValue(seconds);
	startCountdown();
}

void BaconsHelperDock::cancelCountdown()
{
	if (!ready_)
		return;
	sendJson("DELETE", QStringLiteral("/api/obs-plugin/countdown"), {},
			 [this](const QJsonObject &, bool ok, int, const QString &) {
		if (ok) {
			countdownEndTime_ = 0;
			updateCountdownStatus();
		}
		setStatus(ok ? QStringLiteral("✓ Countdown cancelled.") :
				  QStringLiteral("⚠ Countdown could not be cancelled."), ok);
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
