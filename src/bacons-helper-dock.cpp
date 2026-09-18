#ifdef BH_HAVE_LIBSECRET
// libsecret pulls in GLib/GDBus declarations with a member named `signals`.
// Include it before Qt so Qt keyword macros can never rewrite GLib headers.
#include <libsecret/secret.h>
#endif

#include "bacons-helper-dock.hpp"

#include <obs-module.h>

#include <QByteArray>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSysInfo>
#include <QTabWidget>
#include <QtNumeric>
#include <QUrl>
#include <QVBoxLayout>

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
	addToggle(channelLayout, QStringLiteral("Bot muted"), QStringLiteral("bot_muted"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Chat notifications"), QStringLiteral("chat_notifications"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Automatic raid shoutout"), QStringLiteral("raid_shoutout"), channelToggles_);
	addToggle(channelLayout, QStringLiteral("Reduced messaging"), QStringLiteral("reduced_messaging"), channelToggles_);
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
	eventsLayout->addStretch();
	tabs->addTab(eventsTab, QStringLiteral("Stream Events"));

	auto *countdownTab = new QWidget();
	auto *countdownLayout = new QVBoxLayout(countdownTab);
	auto *countdownRow = new QHBoxLayout();
	countdownSeconds_ = new QSpinBox();
	countdownSeconds_->setRange(1, 86400);
	countdownSeconds_->setValue(60);
	countdownSeconds_->setSuffix(QStringLiteral(" seconds"));
	auto *startButton = new QPushButton(QStringLiteral("Start Countdown"));
	auto *cancelButton = new QPushButton(QStringLiteral("Cancel"));
	countdownRow->addWidget(countdownSeconds_);
	countdownRow->addWidget(startButton);
	countdownRow->addWidget(cancelButton);
	countdownStatus_ = new QLabel(QStringLiteral("Countdown: inactive"));
	countdownStatus_->setWordWrap(true);
	countdownLayout->addLayout(countdownRow);
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
	connect(startButton, &QPushButton::clicked, this, &BaconsHelperDock::startCountdown);
	connect(cancelButton, &QPushButton::clicked, this, &BaconsHelperDock::cancelCountdown);

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
	countdownSeconds_->setEnabled(enabled);
	refreshButton_->setEnabled(enabled);
	disconnectButton_->setEnabled(enabled);
}

bool BaconsHelperDock::validateServerUrl()
{
	QUrl url(serverUrl_->text().trimmed());
	if (!url.isValid() || url.scheme().toLower() != QStringLiteral("https") || url.host().isEmpty()) {
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

QNetworkReply *BaconsHelperDock::sendJson(const QByteArray &method, const QString &path, const QJsonObject &payload)
{
	QNetworkRequest request(endpoint(path));
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
		const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		if (reply->error() != QNetworkReply::NoError || !doc.object().value(QStringLiteral("ok")).toBool()) {
			setStatus(QStringLiteral("✕ Pairing failed. Check the code or generate a new one."), false);
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
	token_.clear();
	dashboardLinks_.clear();
	QString credentialError;
	const bool cleared = saveLocalSettings(&credentialError);
	credentialStorageWarning_ = cleared ? QString() : credentialError;
	setControlsEnabled(false);
	channel_->setText(QStringLiteral("Channel: not paired"));
	setStatus(QStringLiteral("✕ Pairing forgotten on this computer. Revoke it on the dashboard if this machine is no longer trusted."), false);
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

	const QJsonObject countdown = payload.value(QStringLiteral("countdown")).toObject();
	if (!countdown.isEmpty() && countdown.value(QStringLiteral("endTime")).toDouble() > QDateTime::currentMSecsSinceEpoch()) {
		const qint64 remaining = qMax<qint64>(0, qRound64((countdown.value(QStringLiteral("endTime")).toDouble() - QDateTime::currentMSecsSinceEpoch()) / 1000.0));
		countdownStatus_->setText(QStringLiteral("Countdown: active, about %1 seconds remaining").arg(remaining));
	} else {
		countdownStatus_->setText(QStringLiteral("Countdown: inactive"));
	}

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
			setStatus(QStringLiteral("⚠ Could not save %1; refreshing its server value.").arg(key), false);
			if (!token_.isEmpty()) refresh();
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
			setStatus(QStringLiteral("⚠ Stream Event setting was not saved; refreshing."), false);
			if (!token_.isEmpty()) refresh();
		} else {
			setStatus(QStringLiteral("✓ Stream Event setting saved."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::startCountdown()
{
	if (token_.isEmpty())
		return;
	QJsonObject payload{{QStringLiteral("durationSeconds"), countdownSeconds_->value()}};
	auto *reply = sendJson("POST", QStringLiteral("/api/obs-plugin/countdown"), payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			setStatus(QStringLiteral("⚠ Countdown could not be started."), false);
		} else {
			countdownStatus_->setText(QStringLiteral("Countdown: active"));
			setStatus(QStringLiteral("✓ Countdown started."), true);
		}
		reply->deleteLater();
	});
}

void BaconsHelperDock::cancelCountdown()
{
	if (token_.isEmpty())
		return;
	auto *reply = sendJson("DELETE", QStringLiteral("/api/obs-plugin/countdown"));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (reply->error() != QNetworkReply::NoError) {
			handleAuthFailure(reply);
			setStatus(QStringLiteral("⚠ Countdown could not be cancelled."), false);
		} else {
			countdownStatus_->setText(QStringLiteral("Countdown: inactive"));
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
