#include "bacons-helper-dock.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTest>
#include <QSslSocket>
#include <QUrlQuery>

#include <cstring>
#include <utility>

class FixtureReply final : public QNetworkReply {
public:
	FixtureReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
	{
		setRequest(request);
		setUrl(request.url());
		open(QIODevice::ReadOnly);
	}

	void finish(int status, QByteArray data)
	{
		body_ = std::move(data);
		setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
		setFinished(true);
		Q_EMIT finished();
	}

	void abort() override
	{
		aborted = true;
		setError(QNetworkReply::OperationCanceledError, QStringLiteral("Cancelled"));
		finish(0, {});
	}

	bool aborted = false;

protected:
	qint64 readData(char *data, qint64 maxSize) override
	{
		if (body_.isEmpty())
			return -1;
		const qint64 count = qMin<qint64>(body_.size(), maxSize);
		std::memcpy(data, body_.constData(), static_cast<size_t>(count));
		body_.remove(0, count);
		return count;
	}

private:
	QByteArray body_;
};

class FixtureNetwork final : public QNetworkAccessManager {
public:
	using QNetworkAccessManager::QNetworkAccessManager;
	QList<FixtureReply *> sent;
	QList<QByteArray> bodies;

protected:
	QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *body) override
	{
		bodies << (body ? body->readAll() : QByteArray{});
		auto *reply = new FixtureReply(request, this);
		sent << reply;
		return reply;
	}
};

class DockTests : public QObject {
	Q_OBJECT

private:
	static FixtureNetwork *attach(BaconsHelperDock &dock)
	{
		auto *network = new FixtureNetwork(&dock);
		dock.network_ = network;
		dock.token_ = QStringLiteral("bh_obs_fixture");
		dock.setControlsEnabled(true);
		return network;
	}

	static constexpr auto settings = R"({"ok":true,"channel":{"displayName":"Example"},"settings":{"translator":false}})";

private Q_SLOTS:
	void sendsEditsInOrderAndDiscardsOutdatedRefresh()
	{
		BaconsHelperDock dock;
		auto *network = attach(dock);
		dock.refresh();
		dock.refresh();
		dock.saveChannelToggle(QStringLiteral("translator"), true);
		dock.saveChannelToggle(QStringLiteral("translator"), false);
		QCOMPARE(network->sent.size(), 1);
		auto *refresh = network->sent.at(0);
		QCOMPARE(refresh->request().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
				 int(QNetworkRequest::SameOriginRedirectPolicy));
		QCOMPARE(refresh->request().rawHeader("Authorization"), "Bearer bh_obs_fixture");
		refresh->finish(200, settings);
		QCOMPARE(dock.channel_->text(), QStringLiteral("Channel: not paired"));
		QCOMPARE(network->sent.size(), 2);
		QCOMPARE(network->bodies.at(1), R"({"settings":{"translator":true}})");
		network->sent.at(1)->finish(200, R"({"ok":true})");
		QCOMPARE(network->bodies.at(2), R"({"settings":{"translator":false}})");
		network->sent.at(2)->finish(200, R"({"ok":true})");
		QCOMPARE(network->sent.size(), 4);
		network->sent.at(3)->finish(200, settings);
		QCOMPARE(dock.channel_->text(), QStringLiteral("Channel: Example"));
	}

	void forgettingAbortsActiveRequestAndDropsQueuedWrites()
	{
		BaconsHelperDock dock;
		auto *network = attach(dock);
		dock.refresh();
		dock.saveChannelToggle(QStringLiteral("translator"), true);
		auto *refresh = network->sent.constFirst();
		dock.disconnectAccount();
		QVERIFY(refresh->aborted);
		QCOMPARE(network->sent.size(), 1);
		QVERIFY(dock.token_.isEmpty());
		QVERIFY(dock.pairButton_->isEnabled());
		QVERIFY(!dock.ready_);
	}

	void unauthorizedClearsAccountAndQueuedWrites()
	{
		BaconsHelperDock dock;
		auto *network = attach(dock);
		dock.saveChannelToggle(QStringLiteral("translator"), true);
		dock.cancelCountdown();
		network->sent.constFirst()->finish(401, R"({"ok":false})");
		QCOMPARE(network->sent.size(), 1);
		QVERIFY(dock.token_.isEmpty());
		QVERIFY(dock.status_->text().contains(QStringLiteral("revoked")));
	}

	void countdownRejectionKeepsCurrentState()
	{
		BaconsHelperDock dock;
		auto *network = attach(dock);
		dock.countdownEndTime_ = 123;
		dock.cancelCountdown();
		network->sent.constFirst()->finish(200, R"({"ok":false})");
		QCOMPARE(dock.countdownEndTime_, 123);
		QVERIFY(dock.status_->text().contains(QStringLiteral("could not be cancelled")));
	}

	void pairingLocksTheServerAndRequiresAnInitialSnapshot()
	{
		if (!QSslSocket::supportsSsl())
			QSKIP("Qt TLS backend unavailable.");
		BaconsHelperDock dock;
		auto *network = new FixtureNetwork(&dock);
		dock.network_ = network;
		dock.pairCode_->setText(QStringLiteral("ABCD-EFGH"));
		dock.pair();
		QVERIFY(!dock.serverUrl_->isEnabled());
		QVERIFY(!dock.pairButton_->isEnabled());
		dock.pair();
		QCOMPARE(network->sent.size(), 1);
		QVERIFY(network->sent.constFirst()->request().rawHeader("Authorization").isEmpty());
		network->sent.at(0)->finish(200, R"({"ok":true,"token":"bh_obs_fixture"})");
		QVERIFY(!dock.ready_);
		QCOMPARE(network->sent.size(), 2);
		network->sent.at(1)->finish(200, settings);
		QVERIFY(dock.ready_);
		QVERIFY(!dock.serverUrl_->isEnabled());
	}

	void rejectedPairingUnlocksTheServer()
	{
		if (!QSslSocket::supportsSsl())
			QSKIP("Qt TLS backend unavailable.");
		BaconsHelperDock dock;
		auto *network = new FixtureNetwork(&dock);
		dock.network_ = network;
		dock.pairCode_->setText(QStringLiteral("ABCD-EFGH"));
		dock.pair();
		network->sent.constFirst()->finish(400, R"({"ok":false,"error":"Pairing code expired"})");
		QVERIFY(dock.token_.isEmpty());
		QVERIFY(dock.serverUrl_->isEnabled());
		QVERIFY(dock.status_->text().contains(QStringLiteral("Pairing code expired")));
	}

	void existingSourcesOnlyMatchTheSameOverlayAndChannel()
	{
		const QUrl countdown(QStringLiteral("https://example.test/overlays/channel/countdown?token=old"));
		QVERIFY(BaconsHelperDock::sameOverlay(countdown,
			QUrl(QStringLiteral("https://example.test/overlays/channel/countdown?token=new"))));
		QVERIFY(!BaconsHelperDock::sameOverlay(countdown,
			QUrl(QStringLiteral("https://example.test/overlays/another/countdown?token=new"))));
		QVERIFY(!BaconsHelperDock::sameOverlay(countdown,
			QUrl(QStringLiteral("https://another.test/overlays/channel/countdown?token=new"))));

		const auto legacy = [](const QByteArray &parameters) {
			QUrl url(QStringLiteral("https://example.test/channel/obs/credits/"));
			QUrlQuery query;
			query.addQueryItem(QStringLiteral("p"), QString::fromLatin1(parameters.toBase64(
				QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
			url.setQuery(query);
			return url;
		};
		const auto credits = legacy(R"({"username":"channel"})");
		QVERIFY(BaconsHelperDock::sameOverlay(credits, legacy(R"({"username":"channel","showBoxArt":true})")));
		QVERIFY(!BaconsHelperDock::sameOverlay(credits, legacy(R"({"username":"another"})")));
	}

	void serverRejectsNonHttpsAndCredentialsInUrl()
	{
		BaconsHelperDock dock;
		dock.serverUrl_->setText(QStringLiteral("http://example.com"));
		QVERIFY(!dock.validateServerUrl());
		dock.serverUrl_->setText(QStringLiteral("https://user:pass@example.com"));
		QVERIFY(!dock.validateServerUrl());
		dock.serverUrl_->setText(QStringLiteral("https://example.com:8443/dashboard?arg=1"));
		QVERIFY(dock.validateServerUrl());
		QCOMPARE(dock.endpoint(QStringLiteral("/api/obs-plugin/settings")).toString(),
				 QStringLiteral("https://example.com:8443/api/obs-plugin/settings"));
	}
};

QTEST_MAIN(DockTests)
#include "dock-tests.moc"
