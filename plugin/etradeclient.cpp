#include "etradeclient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {
constexpr auto kOrgName = "org.von.etrade";
constexpr auto kConfigName = "config.json";
constexpr auto kHistoryName = "history.json";
constexpr auto kLogName = "debug.log";

QString compactBody(const QByteArray &body) {
    auto text = QString::fromUtf8(body).simplified();
    if (text.size() > 600) {
        text = text.left(600) + QStringLiteral("...");
    }
    return text;
}

QString tokenState(const QString &token) {
    return token.isEmpty() ? QStringLiteral("empty")
                           : QStringLiteral("set(len=%1)").arg(token.size());
}

QString historyScopeName(bool sandbox, const QString &accountIdKey) {
    const auto mode = sandbox ? QStringLiteral("sandbox") : QStringLiteral("live");
    if (accountIdKey.isEmpty()) {
        return mode + QStringLiteral("-default");
    }

    const auto keyHash = QString::fromLatin1(QCryptographicHash::hash(accountIdKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    return mode + QLatin1Char('-') + keyHash;
}

QString transactionQueryDate(const QDate &date) {
    return date.toString(QStringLiteral("MMddyyyy"));
}
}

ETradeClient::ETradeClient(QObject *parent)
    : QObject(parent) {
}

bool ETradeClient::authenticated() const {
    return !m_accessToken.isEmpty() && !m_accessTokenSecret.isEmpty();
}

QString ETradeClient::storagePath() const {
    const auto baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(baseDir).filePath(kOrgName);
}

QString ETradeClient::logFilePath() const {
    return logPath();
}

QString ETradeClient::selectedAccountLabel() const {
    for (const auto &variant : m_accounts) {
        const auto map = variant.toMap();
        if (map.value(QStringLiteral("accountIdKey")).toString() == m_accountIdKey) {
            return map.value(QStringLiteral("label")).toString();
        }
    }
    return m_accountIdKey;
}

QString ETradeClient::trackingStartLabel() const {
    return m_trackingStartDate.isValid()
        ? m_trackingStartDate.toString(QStringLiteral("MMM d, yyyy"))
        : QString();
}

void ETradeClient::setSandbox(bool sandbox) {
    if (m_sandbox == sandbox) {
        return;
    }
    m_sandbox = sandbox;
    loadHistory();
    writeLog(QStringLiteral("mode changed sandbox=%1 historyFile=%2")
             .arg(m_sandbox ? QStringLiteral("true") : QStringLiteral("false"), historyFilePath()));
    emit sandboxChanged();
}

void ETradeClient::setConsumerKey(const QString &consumerKey) {
    if (m_consumerKey == consumerKey) {
        return;
    }
    m_consumerKey = consumerKey.trimmed();
    emit credentialsChanged();
}

void ETradeClient::setConsumerSecret(const QString &consumerSecret) {
    if (m_consumerSecret == consumerSecret) {
        return;
    }
    m_consumerSecret = consumerSecret.trimmed();
    emit credentialsChanged();
}

void ETradeClient::setAccountIdKey(const QString &accountIdKey) {
    const auto cleaned = accountIdKey.trimmed();
    if (m_accountIdKey == cleaned) {
        return;
    }
    m_accountIdKey = cleaned;
    loadHistory();
    writeLog(QStringLiteral("account changed accountIdKey=%1 historyFile=%2")
             .arg(m_accountIdKey, historyFilePath()));
    emit accountIdKeyChanged();
    emit accountsChanged();
}

void ETradeClient::setRefreshMinutes(int refreshMinutes) {
    const auto bounded = std::clamp(refreshMinutes, 1, 240);
    if (m_refreshMinutes == bounded) {
        return;
    }
    m_refreshMinutes = bounded;
    emit refreshMinutesChanged();
}

void ETradeClient::setChartMonths(int chartMonths) {
    const auto bounded = std::clamp(chartMonths, 0, 12);
    if (m_chartMonths == bounded) {
        return;
    }
    m_chartMonths = bounded;
    updateChartPoints();
    emit chartMonthsChanged();
}

void ETradeClient::setLoading(bool loading) {
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void ETradeClient::setLastError(const QString &lastError) {
    if (m_lastError == lastError) {
        return;
    }
    m_lastError = lastError;
    emit lastErrorChanged();
}

void ETradeClient::setStatusText(const QString &statusText) {
    if (m_statusText == statusText) {
        return;
    }
    m_statusText = statusText;
    emit statusTextChanged();
}

QString ETradeClient::configFilePath() const {
    return QDir(storagePath()).filePath(kConfigName);
}

QString ETradeClient::historyFilePath() const {
    const auto scope = historyScopeName(m_sandbox, m_accountIdKey);
    return QDir(storagePath()).filePath(QStringLiteral("history-") + scope + QStringLiteral(".json"));
}

QString ETradeClient::logPath() const {
    return QDir(storagePath()).filePath(kLogName);
}

QString ETradeClient::apiBaseUrl() const {
    return m_sandbox ? QStringLiteral("https://apisb.etrade.com")
                     : QStringLiteral("https://api.etrade.com");
}

QString ETradeClient::authorizeBaseUrl() const {
    return QStringLiteral("https://us.etrade.com/e/t/etws/authorize");
}

void ETradeClient::loadSettings() {
    QDir().mkpath(storagePath());
    QFile file(configFilePath());
    if (!file.exists()) {
        writeLog(QStringLiteral("settings missing; starting fresh storage=%1").arg(storagePath()));
        loadHistory();
        updateChartPoints();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("Could not read settings from %1").arg(configFilePath()));
        writeLog(QStringLiteral("settings read failed path=%1").arg(configFilePath()));
        return;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        setLastError(QStringLiteral("Settings file is not valid JSON."));
        writeLog(QStringLiteral("settings parse failed path=%1").arg(configFilePath()));
        return;
    }

    const auto root = doc.object();
    setSandbox(root.value(QStringLiteral("sandbox")).toBool(false));
    setConsumerKey(root.value(QStringLiteral("consumerKey")).toString());
    setConsumerSecret(root.value(QStringLiteral("consumerSecret")).toString());
    setAccountIdKey(root.value(QStringLiteral("accountIdKey")).toString());
    setRefreshMinutes(root.value(QStringLiteral("refreshMinutes")).toInt(m_refreshMinutes));
    setChartMonths(root.value(QStringLiteral("chartMonths")).toInt(m_chartMonths));

    m_requestToken = root.value(QStringLiteral("requestToken")).toString();
    m_requestTokenSecret = root.value(QStringLiteral("requestTokenSecret")).toString();
    m_loginUrl = root.value(QStringLiteral("loginUrl")).toString();
    emit requestTokenChanged();
    emit loginUrlChanged();

    const auto oldAuthenticated = authenticated();
    m_accessToken = root.value(QStringLiteral("accessToken")).toString();
    m_accessTokenSecret = root.value(QStringLiteral("accessTokenSecret")).toString();
    if (oldAuthenticated != authenticated()) {
        emit authenticatedChanged();
    }
    emit accessTokenChanged();

    loadHistory();
    updateChartPoints();
    setLastError({});
    writeLog(QStringLiteral("settings loaded sandbox=%1 accountIdKey=%2 accessToken=%3 historyPoints=%4")
             .arg(m_sandbox ? QStringLiteral("true") : QStringLiteral("false"),
                  m_accountIdKey,
                  tokenState(m_accessToken),
                  QString::number(m_history.size())));
}

bool ETradeClient::saveSettings() {
    QDir().mkpath(storagePath());

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError(QStringLiteral("Could not write settings to %1").arg(configFilePath()));
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("sandbox"), m_sandbox);
    root.insert(QStringLiteral("consumerKey"), m_consumerKey);
    root.insert(QStringLiteral("consumerSecret"), m_consumerSecret);
    root.insert(QStringLiteral("accountIdKey"), m_accountIdKey);
    root.insert(QStringLiteral("requestToken"), m_requestToken);
    root.insert(QStringLiteral("requestTokenSecret"), m_requestTokenSecret);
    root.insert(QStringLiteral("loginUrl"), m_loginUrl);
    root.insert(QStringLiteral("accessToken"), m_accessToken);
    root.insert(QStringLiteral("accessTokenSecret"), m_accessTokenSecret);
    root.insert(QStringLiteral("refreshMinutes"), m_refreshMinutes);
    root.insert(QStringLiteral("chartMonths"), m_chartMonths);

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    setLastError({});
    writeLog(QStringLiteral("settings saved sandbox=%1 accountIdKey=%2 accessToken=%3")
             .arg(m_sandbox ? QStringLiteral("true") : QStringLiteral("false"),
                  m_accountIdKey,
                  tokenState(m_accessToken)));
    return true;
}

void ETradeClient::clearSession() {
    const auto wasAuthenticated = authenticated();
    m_requestToken.clear();
    m_requestTokenSecret.clear();
    m_loginUrl.clear();
    m_accessToken.clear();
    m_accessTokenSecret.clear();
    m_accountIdKey.clear();
    m_accounts.clear();
    m_positions.clear();
    m_totalValue = 0.0;
    m_positionsValue = 0.0;
    m_cashBalance = 0.0;
    m_totalGainLoss = 0.0;
    m_totalGainLossPct = 0.0;
    m_todaysGainLoss = 0.0;
    m_todaysGainLossPct = 0.0;
    m_netInvested = 0.0;
    m_profitLoss = 0.0;
    m_profitLossPct = 0.0;
    m_dailyPerformance = 0.0;
    m_peakValue = 0.0;
    m_drawdown = 0.0;
    m_drawdownPct = 0.0;
    m_pendingTransactionFlows.clear();
    m_trackingStartDate = {};
    m_trackingBaselineValue = 0.0;
    m_lastTransactionSyncDate = {};
    m_history.clear();
    m_chartPoints.clear();
    emit requestTokenChanged();
    emit loginUrlChanged();
    emit accessTokenChanged();
    emit accountIdKeyChanged();
    emit accountsChanged();
    emit positionsChanged();
    emit chartPointsChanged();
    emit summaryChanged();
    writeLog(QStringLiteral("session cleared"));
    if (wasAuthenticated) {
        emit authenticatedChanged();
    }
    saveSettings();
}

void ETradeClient::writeLog(const QString &message) const {
    QDir().mkpath(storagePath());
    QFile file(logPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << message << '\n';
}

void ETradeClient::writeReplyLog(const QString &label, QNetworkReply *reply, const QByteArray &body) const {
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    writeLog(QStringLiteral("%1 status=%2 netError=%3 errorText=%4 body=%5")
             .arg(label,
                  QString::number(status),
                  QString::number(static_cast<int>(reply->error())),
                  reply->errorString(),
                  compactBody(body)));
}

bool ETradeClient::replyLooksLikeInvalidAccessToken(QNetworkReply *reply, const QByteArray &body) const {
    if (reply->error() == QNetworkReply::NoError) {
        return false;
    }

    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto text = QString::fromUtf8(body).toLower();
    return (status == 401 || status == 403 || status == 400)
        && text.contains(QStringLiteral("invalid access token"));
}

QString ETradeClient::percentEncode(const QString &value) const {
    static const QByteArray safe = "-._~";
    return QString::fromLatin1(QUrl::toPercentEncoding(value, safe, QByteArray()));
}

QList<QPair<QString, QString>> ETradeClient::normalizedParameters(
    const QList<QPair<QString, QString>> &oauthParameters,
    const QList<QPair<QString, QString>> &queryParameters) const {
    QList<QPair<QString, QString>> all = oauthParameters;
    all.append(queryParameters);

    std::sort(all.begin(), all.end(), [&](const auto &left, const auto &right) {
        const auto leftKey = percentEncode(left.first);
        const auto rightKey = percentEncode(right.first);
        if (leftKey != rightKey) {
            return leftKey < rightKey;
        }
        return percentEncode(left.second) < percentEncode(right.second);
    });
    return all;
}

QByteArray ETradeClient::hmacSha1(const QByteArray &key, const QByteArray &message) const {
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha1).toBase64();
}

QByteArray ETradeClient::buildAuthorizationHeader(const QString &method,
                                                  const QUrl &url,
                                                  const QList<QPair<QString, QString>> &extraOAuth,
                                                  const QList<QPair<QString, QString>> &queryParameters,
                                                  const QString &tokenSecret,
                                                  const QString &oauthTokenOverride) const {
    QList<QPair<QString, QString>> oauthParameters = {
        {QStringLiteral("oauth_consumer_key"), m_consumerKey},
        {QStringLiteral("oauth_nonce"), QString::number(QRandomGenerator::global()->generate64())},
        {QStringLiteral("oauth_signature_method"), QStringLiteral("HMAC-SHA1")},
        {QStringLiteral("oauth_timestamp"), QString::number(QDateTime::currentSecsSinceEpoch())}
    };

    const auto oauthToken = oauthTokenOverride.isEmpty() ? m_accessToken : oauthTokenOverride;
    if (!oauthToken.isEmpty()) {
        oauthParameters.append({QStringLiteral("oauth_token"), oauthToken});
    }

    for (const auto &pair : extraOAuth) {
        oauthParameters.append(pair);
    }

    const auto allParameters = normalizedParameters(oauthParameters, queryParameters);
    QStringList encodedPairs;
    encodedPairs.reserve(allParameters.size());
    for (const auto &pair : allParameters) {
        encodedPairs.append(percentEncode(pair.first) + '=' + percentEncode(pair.second));
    }

    const auto normalized = encodedPairs.join('&');
    const QString baseString = method.toUpper() + '&'
        + percentEncode(url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment)) + '&'
        + percentEncode(normalized);
    const auto signingKey = percentEncode(m_consumerSecret).toUtf8() + '&' + percentEncode(tokenSecret).toUtf8();
    const auto signature = QString::fromLatin1(hmacSha1(signingKey, baseString.toUtf8()));

    oauthParameters.append({QStringLiteral("oauth_signature"), signature});

    QStringList headerPairs;
    for (const auto &pair : oauthParameters) {
        headerPairs.append(percentEncode(pair.first) + "=\"" + percentEncode(pair.second) + "\"");
    }

    return QByteArray("OAuth realm=\"\", ") + headerPairs.join(", ").toUtf8();
}

ETradeClient::OAuthToken ETradeClient::parseTokenReply(const QByteArray &body) const {
    OAuthToken token;

    const auto pairs = body.split('&');
    for (const auto &pair : pairs) {
        const auto separator = pair.indexOf('=');
        const auto rawKey = separator >= 0 ? pair.left(separator) : pair;
        const auto rawValue = separator >= 0 ? pair.mid(separator + 1) : QByteArray();
        const auto key = QUrl::fromPercentEncoding(rawKey);
        const auto value = QUrl::fromPercentEncoding(rawValue);

        if (key == QStringLiteral("oauth_token")) {
            token.token = value;
        } else if (key == QStringLiteral("oauth_token_secret")) {
            token.secret = value;
        }
    }

    return token;
}

void ETradeClient::startAuthorization() {
    if (m_consumerKey.isEmpty() || m_consumerSecret.isEmpty()) {
        setLastError(QStringLiteral("Enter your E*TRADE consumer key and consumer secret first."));
        return;
    }

    setLoading(true);
    setLastError({});
    setStatusText(QStringLiteral("Requesting E*TRADE login token..."));
    writeLog(QStringLiteral("request_token start sandbox=%1 consumerKey=%2")
             .arg(m_sandbox ? QStringLiteral("true") : QStringLiteral("false"),
                  tokenState(m_consumerKey)));

    QUrl url(apiBaseUrl() + QStringLiteral("/oauth/request_token"));
    QNetworkRequest request(url);
    const auto header = buildAuthorizationHeader(QStringLiteral("GET"),
                                                 url,
                                                 {{QStringLiteral("oauth_callback"), QStringLiteral("oob")}},
                                                 {},
                                                 {});
    request.setRawHeader("Authorization", header);
    request.setRawHeader("Accept", "application/json");

    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setLoading(false);
        const auto body = reply->readAll();
        const auto oldAuthenticated = authenticated();
        writeReplyLog(QStringLiteral("request_token"), reply, body);
        if (reply->error() != QNetworkReply::NoError) {
            setLastError(QStringLiteral("Request token failed: %1").arg(QString::fromUtf8(body)));
            setStatusText(QStringLiteral("Request token failed."));
            reply->deleteLater();
            if (oldAuthenticated != authenticated()) {
                emit authenticatedChanged();
            }
            return;
        }

        const auto token = parseTokenReply(body);
        if (token.token.isEmpty() || token.secret.isEmpty()) {
            setLastError(QStringLiteral("E*TRADE did not return a valid request token."));
            setStatusText(QStringLiteral("Authorization setup failed."));
            reply->deleteLater();
            return;
        }

        m_requestToken = token.token;
        m_requestTokenSecret = token.secret;

        QUrl authorizeUrl(authorizeBaseUrl());
        QUrlQuery authorizeQuery;
        authorizeQuery.addQueryItem(QStringLiteral("key"), m_consumerKey);
        authorizeQuery.addQueryItem(QStringLiteral("token"), m_requestToken);
        authorizeUrl.setQuery(authorizeQuery);
        m_loginUrl = authorizeUrl.toString();

        emit requestTokenChanged();
        emit loginUrlChanged();
        saveSettings();
        writeLog(QStringLiteral("request_token ok requestToken=%1 loginUrlReady=true")
                 .arg(tokenState(m_requestToken)));
        setStatusText(QStringLiteral("Open the login URL within 5 minutes, approve access, then paste the verifier code below."));
        setLastError({});
        reply->deleteLater();
    });
}

void ETradeClient::completeAuthorization(const QString &verifier) {
    if (m_requestToken.isEmpty() || m_requestTokenSecret.isEmpty()) {
        setLastError(QStringLiteral("Click \"1. Start E*TRADE Login\" first. That creates a temporary request token; it can disappear if the widget restarts or expire after a few minutes."));
        return;
    }

    const auto trimmedVerifier = verifier.trimmed();
    if (trimmedVerifier.isEmpty()) {
        setLastError(QStringLiteral("Paste the verifier code from E*TRADE first."));
        return;
    }

    setLoading(true);
    setLastError({});
    setStatusText(QStringLiteral("Finishing E*TRADE authorization..."));
    writeLog(QStringLiteral("access_token start verifierLen=%1 requestToken=%2")
             .arg(QString::number(trimmedVerifier.size()), tokenState(m_requestToken)));

    QUrl url(apiBaseUrl() + QStringLiteral("/oauth/access_token"));
    QNetworkRequest request(url);
    const auto header = buildAuthorizationHeader(QStringLiteral("GET"),
                                                 url,
                                                 {{QStringLiteral("oauth_verifier"), trimmedVerifier}},
                                                 {},
                                                 m_requestTokenSecret,
                                                 m_requestToken);

    request.setRawHeader("Authorization", header);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setLoading(false);
        const auto body = reply->readAll();
        writeReplyLog(QStringLiteral("access_token"), reply, body);
        if (reply->error() != QNetworkReply::NoError) {
            if (QString::fromUtf8(body).contains(QStringLiteral("token_rejected"), Qt::CaseInsensitive)) {
                m_requestToken.clear();
                m_requestTokenSecret.clear();
                m_loginUrl.clear();
                emit requestTokenChanged();
                emit loginUrlChanged();
                saveSettings();
                setLastError(QStringLiteral("That verifier/request-token pair was rejected. Click \"1. Start E*TRADE Login\" again to generate a fresh request token, then retry within a few minutes."));
            } else {
                setLastError(QStringLiteral("Access token failed: %1").arg(QString::fromUtf8(body)));
            }
            setStatusText(QStringLiteral("Authorization failed."));
            reply->deleteLater();
            return;
        }

        const auto token = parseTokenReply(body);
        if (token.token.isEmpty() || token.secret.isEmpty()) {
            setLastError(QStringLiteral("E*TRADE did not return a valid access token."));
            setStatusText(QStringLiteral("Authorization failed."));
            reply->deleteLater();
            return;
        }

        const auto wasAuthenticated = authenticated();
        m_accessToken = token.token;
        m_accessTokenSecret = token.secret;
        m_requestToken.clear();
        m_requestTokenSecret.clear();
        emit requestTokenChanged();
        emit accessTokenChanged();
        writeLog(QStringLiteral("access_token ok accessToken=%1").arg(tokenState(m_accessToken)));
        if (wasAuthenticated != authenticated()) {
            emit authenticatedChanged();
        }
        saveSettings();
        setStatusText(QStringLiteral("Authorized. Loading accounts..."));
        setLastError({});
        reply->deleteLater();
        refresh();
    });
}

void ETradeClient::sendSignedGet(const QUrl &url, const std::function<void(QNetworkReply *)> &onFinished) {
    QUrlQuery query(url);
    QList<QPair<QString, QString>> queryParameters;
    const auto items = query.queryItems(QUrl::FullyDecoded);
    queryParameters.reserve(items.size());
    for (const auto &item : items) {
        queryParameters.append(item);
    }

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", buildAuthorizationHeader(QStringLiteral("GET"), url, {}, queryParameters, m_accessTokenSecret));
    request.setRawHeader("Accept", "application/json");

    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, onFinished]() {
        onFinished(reply);
    });
}

void ETradeClient::renewAccessToken() {
    if (!authenticated()) {
        setLastError(QStringLiteral("No access token is stored yet, so there is nothing to renew."));
        writeLog(QStringLiteral("renew_access_token skipped no token"));
        return;
    }

    setLoading(true);
    setLastError({});
    setStatusText(QStringLiteral("Renewing E*TRADE access token..."));
    writeLog(QStringLiteral("renew_access_token start accessToken=%1").arg(tokenState(m_accessToken)));

    QUrl url(apiBaseUrl() + QStringLiteral("/oauth/renew_access_token"));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", buildAuthorizationHeader(QStringLiteral("GET"), url, {}, {}, m_accessTokenSecret));

    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto body = reply->readAll();
        writeReplyLog(QStringLiteral("renew_access_token"), reply, body);

        if (reply->error() != QNetworkReply::NoError) {
            setLoading(false);
            m_retryingAfterRenew = false;
            setLastError(QStringLiteral("Token renew failed: %1").arg(QString::fromUtf8(body)));
            setStatusText(QStringLiteral("Token renew failed. Clear tokens and sign in again."));
            reply->deleteLater();
            return;
        }

        setLastError({});
        setStatusText(QStringLiteral("Access token renewed. Retrying portfolio refresh..."));
        writeLog(QStringLiteral("renew_access_token ok"));
        reply->deleteLater();
        retryRefreshAfterRenew();
    });
}

void ETradeClient::retryRefreshAfterRenew() {
    m_retryingAfterRenew = false;
    refresh();
}

void ETradeClient::fetchTransactionsPage(const QDate &startDate, const QDate &endDate, const QString &marker) {
    QUrl url(apiBaseUrl() + QStringLiteral("/v1/accounts/%1/transactions").arg(m_accountIdKey));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("startDate"), transactionQueryDate(startDate));
    query.addQueryItem(QStringLiteral("endDate"), transactionQueryDate(endDate));
    query.addQueryItem(QStringLiteral("count"), QStringLiteral("50"));
    query.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("ASC"));
    if (!marker.isEmpty()) {
        query.addQueryItem(QStringLiteral("marker"), marker);
    }
    url.setQuery(query);

    sendSignedGet(url, [this, startDate, endDate](QNetworkReply *reply) {
        processTransactionsReply(reply, startDate, endDate);
    });
}

void ETradeClient::refresh() {
    if (m_consumerKey.isEmpty() || m_consumerSecret.isEmpty()) {
        setLastError(QStringLiteral("Enter your E*TRADE API key and secret, then authorize the widget."));
        return;
    }
    if (!authenticated()) {
        setLastError(QStringLiteral("Authorize the widget with E*TRADE before refreshing."));
        return;
    }

    setLoading(true);
    setLastError({});
    setStatusText(QStringLiteral("Loading E*TRADE accounts..."));
    m_refreshStage = RefreshStage::ListingAccounts;
    writeLog(QStringLiteral("refresh start sandbox=%1 accountIdKey=%2 accessToken=%3")
             .arg(m_sandbox ? QStringLiteral("true") : QStringLiteral("false"),
                  m_accountIdKey,
                  tokenState(m_accessToken)));

    const QUrl url(apiBaseUrl() + QStringLiteral("/v1/accounts/list.json"));
    sendSignedGet(url, [this](QNetworkReply *reply) {
        processAccountsReply(reply);
    });
}

void ETradeClient::processAccountsReply(QNetworkReply *reply) {
    const auto body = reply->readAll();
    writeReplyLog(QStringLiteral("accounts_list"), reply, body);

    if (replyLooksLikeInvalidAccessToken(reply, body) && !m_retryingAfterRenew) {
        writeLog(QStringLiteral("accounts_list invalid access token; attempting renew"));
        m_retryingAfterRenew = true;
        reply->deleteLater();
        renewAccessToken();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        m_retryingAfterRenew = false;
        setLoading(false);
        setLastError(QStringLiteral("Account list failed: %1").arg(QString::fromUtf8(body)));
        setStatusText(QStringLiteral("Could not load accounts."));
        reply->deleteLater();
        return;
    }

    m_retryingAfterRenew = false;

    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        setLoading(false);
        setLastError(QStringLiteral("Account list returned invalid JSON."));
        setStatusText(QStringLiteral("Could not load accounts."));
        reply->deleteLater();
        return;
    }

    const auto root = doc.object().value(QStringLiteral("AccountListResponse")).toObject();
    QVariantList accounts;

    const auto accountsValue = root.value(QStringLiteral("Accounts"));
    QJsonArray accountArray;
    if (accountsValue.isObject()) {
        accountArray = accountsValue.toObject().value(QStringLiteral("Account")) .toArray();
    }
    if (accountArray.isEmpty() && root.value(QStringLiteral("Accounts")) .isArray()) {
        accountArray = root.value(QStringLiteral("Accounts")).toArray();
    }
    if (accountArray.isEmpty() && root.value(QStringLiteral("Account")) .isArray()) {
        accountArray = root.value(QStringLiteral("Account")).toArray();
    }

    for (const auto &value : accountArray) {
        const auto object = value.toObject();
        const auto description = object.value(QStringLiteral("accountDesc")).toString();
        const auto idKey = object.value(QStringLiteral("accountIdKey")).toString();
        QVariantMap map;
        map.insert(QStringLiteral("accountIdKey"), idKey);
        map.insert(QStringLiteral("accountId"), object.value(QStringLiteral("accountId")).toString());
        map.insert(QStringLiteral("institutionType"), object.value(QStringLiteral("institutionType")).toString());
        map.insert(QStringLiteral("accountStatus"), object.value(QStringLiteral("accountStatus")).toString());
        map.insert(QStringLiteral("label"), description.isEmpty() ? idKey : description + QStringLiteral(" (") + idKey + ')');
        accounts.append(map);
    }

    m_accounts = accounts;
    emit accountsChanged();

    bool currentKeyExists = false;
    for (const auto &variant : m_accounts) {
        if (variant.toMap().value(QStringLiteral("accountIdKey")).toString() == m_accountIdKey) {
            currentKeyExists = true;
            break;
        }
    }

    if ((!currentKeyExists || m_accountIdKey.isEmpty()) && !m_accounts.isEmpty()) {
        QString preferredAccountIdKey;
        for (const auto &variant : m_accounts) {
            const auto map = variant.toMap();
            if (map.value(QStringLiteral("accountStatus")).toString() == QStringLiteral("ACTIVE")) {
                preferredAccountIdKey = map.value(QStringLiteral("accountIdKey")).toString();
                break;
            }
        }
        if (preferredAccountIdKey.isEmpty()) {
            preferredAccountIdKey = m_accounts.first().toMap().value(QStringLiteral("accountIdKey")).toString();
        }
        if (preferredAccountIdKey != m_accountIdKey) {
            writeLog(QStringLiteral("account selection updated old=%1 new=%2")
                     .arg(m_accountIdKey, preferredAccountIdKey));
            setAccountIdKey(preferredAccountIdKey);
            saveSettings();
        }
    }

    if (m_accountIdKey.isEmpty()) {
        setLoading(false);
        setLastError(QStringLiteral("No E*TRADE accounts were returned for this API user."));
        setStatusText(QStringLiteral("No accounts found."));
        reply->deleteLater();
        return;
    }

    setStatusText(QStringLiteral("Loading positions for %1...").arg(m_accountIdKey));
    m_refreshStage = RefreshStage::FetchingPortfolio;

    QUrl url(apiBaseUrl() + QStringLiteral("/v1/accounts/%1/portfolio").arg(m_accountIdKey));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("view"), QStringLiteral("COMPLETE"));
    query.addQueryItem(QStringLiteral("totalsRequired"), QStringLiteral("true"));
    url.setQuery(query);

    sendSignedGet(url, [this](QNetworkReply *portfolioReply) {
        processPortfolioReply(portfolioReply);
    });

    reply->deleteLater();
}

double ETradeClient::readDouble(const QJsonValue &value, double fallback) {
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const auto parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

QString ETradeClient::readString(const QJsonValue &value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return {};
}

QDate ETradeClient::readDate(const QJsonValue &value) {
    if (value.isDouble()) {
        const auto msecs = static_cast<qint64>(value.toDouble());
        return msecs > 0 ? QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC).date() : QDate();
    }
    if (value.isString()) {
        bool ok = false;
        const auto msecs = value.toString().toLongLong(&ok);
        if (ok && msecs > 0) {
            return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC).date();
        }
        const auto isoDate = QDate::fromString(value.toString(), Qt::ISODate);
        return isoDate.isValid() ? isoDate : QDate();
    }
    return {};
}

bool ETradeClient::containsKeyword(const QString &text, const QStringList &keywords) {
    for (const auto &keyword : keywords) {
        if (text.contains(keyword, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QVariantMap ETradeClient::positionToMap(const QJsonObject &positionObject) {
    QVariantMap map;
    const auto product = positionObject.value(QStringLiteral("Product")).toObject();
    const auto quick = positionObject.value(QStringLiteral("Quick")).toObject();
    const auto quoteDetails = positionObject.value(QStringLiteral("QuoteDetails")).toObject();

    auto symbol = product.value(QStringLiteral("symbol")).toString();
    if (symbol.isEmpty()) {
        symbol = positionObject.value(QStringLiteral("symbolDescription")).toString();
    }

    auto name = product.value(QStringLiteral("description")).toString();
    if (name.isEmpty()) {
        name = positionObject.value(QStringLiteral("description")).toString();
    }
    if (name.isEmpty()) {
        name = symbol;
    }

    auto securityType = product.value(QStringLiteral("securityType")).toString();
    if (securityType.isEmpty()) {
        securityType = positionObject.value(QStringLiteral("positionType")).toString();
    }

    const auto quantity = readDouble(positionObject.value(QStringLiteral("quantity")));
    const auto marketValue = readDouble(positionObject.value(QStringLiteral("marketValue")));
    const auto totalGain = readDouble(positionObject.value(QStringLiteral("totalGain")), readDouble(positionObject.value(QStringLiteral("totalGainLoss"))));
    const auto totalGainPct = readDouble(positionObject.value(QStringLiteral("totalGainPct")), readDouble(positionObject.value(QStringLiteral("totalGainLossPct"))));
    const auto daysGain = readDouble(positionObject.value(QStringLiteral("daysGain")), readDouble(positionObject.value(QStringLiteral("daysGainLoss"))));
    const auto daysGainPct = readDouble(positionObject.value(QStringLiteral("daysGainPct")), readDouble(positionObject.value(QStringLiteral("daysGainLossPct"))));
    const auto lastTrade = readDouble(positionObject.value(QStringLiteral("currentPrice")),
                                      readDouble(quick.value(QStringLiteral("lastTrade")),
                                                 readDouble(quoteDetails.value(QStringLiteral("lastTrade")))));

    map.insert(QStringLiteral("symbol"), symbol);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("securityType"), securityType);
    map.insert(QStringLiteral("quantity"), quantity);
    map.insert(QStringLiteral("marketValue"), marketValue);
    map.insert(QStringLiteral("totalGainLoss"), totalGain);
    map.insert(QStringLiteral("totalGainLossPct"), totalGainPct);
    map.insert(QStringLiteral("daysGainLoss"), daysGain);
    map.insert(QStringLiteral("daysGainLossPct"), daysGainPct);
    map.insert(QStringLiteral("lastTrade"), lastTrade);
    map.insert(QStringLiteral("pctOfPortfolio"), readDouble(positionObject.value(QStringLiteral("pctOfPortfolio"))));
    map.insert(QStringLiteral("pricePaid"), readDouble(positionObject.value(QStringLiteral("pricePaid"))));
    return map;
}

void ETradeClient::processPortfolioReply(QNetworkReply *reply) {
    const auto body = reply->readAll();
    writeReplyLog(QStringLiteral("portfolio"), reply, body);

    if (replyLooksLikeInvalidAccessToken(reply, body) && !m_retryingAfterRenew) {
        writeLog(QStringLiteral("portfolio invalid access token; attempting renew"));
        m_retryingAfterRenew = true;
        reply->deleteLater();
        renewAccessToken();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        m_retryingAfterRenew = false;
        setLoading(false);
        setLastError(QStringLiteral("Portfolio failed: %1").arg(QString::fromUtf8(body)));
        setStatusText(QStringLiteral("Could not load positions."));
        reply->deleteLater();
        return;
    }

    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        setLoading(false);
        setLastError(QStringLiteral("Portfolio returned invalid JSON."));
        setStatusText(QStringLiteral("Could not load positions."));
        reply->deleteLater();
        return;
    }

    auto root = doc.object().value(QStringLiteral("PortfolioResponse")).toObject();
    if (root.isEmpty()) {
        root = doc.object();
    }

    QVariantList positions;
    double positionsValue = 0.0;
    double totalGainLoss = 0.0;
    double totalGainLossPct = 0.0;
    double todaysGainLoss = 0.0;
    double todaysGainLossPct = 0.0;
    double cashBalance = 0.0;

    auto rootTotals = root.value(QStringLiteral("Totals")).toObject();
    if (rootTotals.isEmpty()) {
        rootTotals = root.value(QStringLiteral("totals")).toObject();
    }
    const bool hasRootTotalGainLossPct = rootTotals.contains(QStringLiteral("totalGainLossPct")) || rootTotals.contains(QStringLiteral("totalGainPct"));
    const bool hasRootTodaysGainLossPct = rootTotals.contains(QStringLiteral("todaysGainLossPct")) || rootTotals.contains(QStringLiteral("daysGainPct"));

    QJsonArray accountPortfolios;
    auto accountPortfolioValue = root.value(QStringLiteral("AccountPortfolio"));
    if (accountPortfolioValue.isUndefined()) {
        accountPortfolioValue = root.value(QStringLiteral("accountPortfolio"));
    }
    if (accountPortfolioValue.isArray()) {
        accountPortfolios = accountPortfolioValue.toArray();
    } else if (accountPortfolioValue.isObject()) {
        accountPortfolios.append(accountPortfolioValue.toObject());
    }

    if (accountPortfolios.isEmpty() && root.value(QStringLiteral("Portfolio")).isObject()) {
        accountPortfolios.append(root.value(QStringLiteral("Portfolio")).toObject());
    }
    if (accountPortfolios.isEmpty() && root.value(QStringLiteral("portfolio")).isObject()) {
        accountPortfolios.append(root.value(QStringLiteral("portfolio")).toObject());
    }

    for (const auto &portfolioValue : accountPortfolios) {
        const auto portfolioObject = portfolioValue.toObject();
        auto totals = portfolioObject.value(QStringLiteral("totals")).toObject();
        if (totals.isEmpty()) {
            totals = portfolioObject.value(QStringLiteral("Totals")).toObject();
        }

        positionsValue += readDouble(totals.value(QStringLiteral("marketValue")), readDouble(totals.value(QStringLiteral("totalMarketValue"))));
        cashBalance += readDouble(totals.value(QStringLiteral("cashBalance")), readDouble(totals.value(QStringLiteral("cashAvailableForInvestment"))));
        totalGainLoss += readDouble(totals.value(QStringLiteral("totalGain")), readDouble(totals.value(QStringLiteral("totalGainLoss"))));
        todaysGainLoss += readDouble(totals.value(QStringLiteral("daysGain")), readDouble(totals.value(QStringLiteral("todayGainLoss"))));

        QJsonArray positionArray;
        auto positionValue = portfolioObject.value(QStringLiteral("Position"));
        if (positionValue.isUndefined()) {
            positionValue = portfolioObject.value(QStringLiteral("position"));
        }
        if (positionValue.isArray()) {
            positionArray = positionValue.toArray();
        } else if (positionValue.isObject()) {
            positionArray.append(positionValue.toObject());
        }

        for (const auto &position : positionArray) {
            positions.append(positionToMap(position.toObject()));
        }
    }

    std::sort(positions.begin(), positions.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("marketValue")).toDouble()
            > right.toMap().value(QStringLiteral("marketValue")).toDouble();
    });

    if (positionsValue <= 0.0) {
        positionsValue = readDouble(rootTotals.value(QStringLiteral("totalMarketValue")),
                                    readDouble(rootTotals.value(QStringLiteral("marketValue")), positionsValue));
    }
    if (cashBalance <= 0.0) {
        cashBalance = readDouble(rootTotals.value(QStringLiteral("cashBalance")),
                                 readDouble(rootTotals.value(QStringLiteral("cashAvailableForInvestment")), cashBalance));
    }
    if (totalGainLoss == 0.0) {
        totalGainLoss = readDouble(rootTotals.value(QStringLiteral("totalGainLoss")),
                                   readDouble(rootTotals.value(QStringLiteral("totalGain")), totalGainLoss));
    }
    if (todaysGainLoss == 0.0) {
        todaysGainLoss = readDouble(rootTotals.value(QStringLiteral("todaysGainLoss")),
                                    readDouble(rootTotals.value(QStringLiteral("todayGainLoss")),
                                               readDouble(rootTotals.value(QStringLiteral("daysGain")), todaysGainLoss)));
    }
    if (hasRootTotalGainLossPct) {
        totalGainLossPct = readDouble(rootTotals.value(QStringLiteral("totalGainLossPct")),
                                      readDouble(rootTotals.value(QStringLiteral("totalGainPct")), totalGainLossPct));
    }
    if (hasRootTodaysGainLossPct) {
        todaysGainLossPct = readDouble(rootTotals.value(QStringLiteral("todaysGainLossPct")),
                                       readDouble(rootTotals.value(QStringLiteral("daysGainPct")), todaysGainLossPct));
    }

    const double totalValue = positionsValue + cashBalance;
    if (!hasRootTotalGainLossPct && totalValue > 0.0) {
        totalGainLossPct = (totalGainLoss / std::max(1.0, totalValue - totalGainLoss)) * 100.0;
    }
    if (!hasRootTodaysGainLossPct && totalValue > 0.0) {
        todaysGainLossPct = (todaysGainLoss / std::max(1.0, totalValue - todaysGainLoss)) * 100.0;
    }

    writeLog(QStringLiteral("portfolio parsed positions=%1 totalValue=%2 totalGainLoss=%3 todaysGainLoss=%4")
             .arg(QString::number(positions.size()),
                  QString::number(totalValue, 'f', 4),
                  QString::number(totalGainLoss, 'f', 4),
                  QString::number(todaysGainLoss, 'f', 4)));

    m_positions = positions;
    m_positionsValue = positionsValue;
    m_cashBalance = cashBalance;
    m_totalValue = totalValue;
    m_totalGainLoss = totalGainLoss;
    m_totalGainLossPct = totalGainLossPct;
    m_todaysGainLoss = todaysGainLoss;
    m_todaysGainLossPct = todaysGainLossPct;

    appendSnapshot(totalValue);
    saveSettings();

    emit positionsChanged();
    emit summaryChanged();

    if (!m_accountIdKey.isEmpty() && m_trackingStartDate.isValid()) {
        const auto today = QDate::currentDate();
        auto startDate = m_lastTransactionSyncDate.isValid()
            ? std::max(m_trackingStartDate, m_lastTransactionSyncDate.addDays(-30))
            : m_trackingStartDate;
        const auto apiWindowStart = today.addYears(-2).addDays(1);
        if (startDate < apiWindowStart) {
            startDate = apiWindowStart;
        }

        m_pendingTransactionFlows.clear();
        setStatusText(QStringLiteral("Syncing cash flows since %1...").arg(startDate.toString(QStringLiteral("MMM d, yyyy"))));
        reply->deleteLater();
        fetchTransactionsPage(startDate, today);
        return;
    }

    m_retryingAfterRenew = false;
    setLoading(false);
    setStatusText(QStringLiteral("Updated %1 positions.").arg(m_positions.size()));
    setLastError({});
    reply->deleteLater();
}

void ETradeClient::processTransactionsReply(QNetworkReply *reply, const QDate &startDate, const QDate &endDate) {
    const auto body = reply->readAll();
    writeReplyLog(QStringLiteral("transactions"), reply, body);

    if (replyLooksLikeInvalidAccessToken(reply, body) && !m_retryingAfterRenew) {
        writeLog(QStringLiteral("transactions invalid access token; attempting renew"));
        m_retryingAfterRenew = true;
        reply->deleteLater();
        renewAccessToken();
        return;
    }

    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && status != 204) {
        m_retryingAfterRenew = false;
        setLoading(false);
        setLastError(QStringLiteral("Portfolio updated, but cash-flow sync failed: %1").arg(QString::fromUtf8(body)));
        setStatusText(QStringLiteral("Updated positions, but cash-flow sync failed."));
        reply->deleteLater();
        return;
    }

    QJsonArray transactions;
    bool moreTransactions = false;
    QString nextMarker;

    if (status != 204 && !body.trimmed().isEmpty()) {
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            m_retryingAfterRenew = false;
            setLoading(false);
            setLastError(QStringLiteral("Portfolio updated, but cash-flow sync returned invalid JSON."));
            setStatusText(QStringLiteral("Updated positions, but cash-flow sync failed."));
            reply->deleteLater();
            return;
        }

        auto root = doc.object().value(QStringLiteral("TransactionListResponse")).toObject();
        if (root.isEmpty()) {
            root = doc.object();
        }

        const auto transactionValue = root.value(QStringLiteral("Transaction")).isUndefined()
            ? root.value(QStringLiteral("transaction"))
            : root.value(QStringLiteral("Transaction"));
        if (transactionValue.isArray()) {
            transactions = transactionValue.toArray();
        } else if (transactionValue.isObject()) {
            transactions.append(transactionValue.toObject());
        }

        moreTransactions = root.value(QStringLiteral("moreTransactions")).toBool(false);
        if (moreTransactions && !transactions.isEmpty()) {
            nextMarker = readString(transactions.last().toObject().value(QStringLiteral("transactionId")));
            if (nextMarker.isEmpty()) {
                nextMarker = readString(root.value(QStringLiteral("marker")));
            }
            if (nextMarker.isEmpty()) {
                nextMarker = readString(root.value(QStringLiteral("pageMarkers")));
            }
        }
    }

    for (const auto &value : transactions) {
        const auto object = value.toObject();
        auto date = readDate(object.value(QStringLiteral("postDate")));
        if (!date.isValid()) {
            date = readDate(object.value(QStringLiteral("transactionDate")));
        }
        if (!date.isValid()) {
            continue;
        }

        auto description = readString(object.value(QStringLiteral("description")));
        description += QLatin1Char(' ') + readString(object.value(QStringLiteral("description2")));

        auto category = object.value(QStringLiteral("category")).toObject();
        if (category.isEmpty()) {
            category = object.value(QStringLiteral("Category")).toObject();
        }

        auto brokerage = object.value(QStringLiteral("brokerage")).toObject();
        if (brokerage.isEmpty()) {
            brokerage = object.value(QStringLiteral("Brokerage")).toObject();
        }

        auto transactionType = readString(object.value(QStringLiteral("transactionType")));
        if (transactionType.isEmpty()) {
            transactionType = readString(brokerage.value(QStringLiteral("transactionType")));
        }

        const auto text = (transactionType
                           + QLatin1Char(' ')
                           + description
                           + QLatin1Char(' ')
                           + readString(category.value(QStringLiteral("categoryName")))
                           + QLatin1Char(' ')
                           + readString(category.value(QStringLiteral("parentCategory")))
                           + QLatin1Char(' ')
                           + readString(object.value(QStringLiteral("displaySymbol"))))
                              .toUpper();
        const auto amount = readDouble(object.value(QStringLiteral("amount")));

        const bool isIncome = containsKeyword(text, {
            QStringLiteral("DIVIDEND"),
            QStringLiteral("INTEREST"),
            QStringLiteral("CAPITAL GAIN"),
            QStringLiteral("GAIN DISTRIBUTION"),
            QStringLiteral("PAYMENT IN LIEU")
        });
        const bool isTrade = containsKeyword(text, {
            QStringLiteral("BOUGHT"),
            QStringLiteral("SOLD"),
            QStringLiteral("PURCHASE"),
            QStringLiteral("REDEMPTION"),
            QStringLiteral("TRADE"),
            QStringLiteral("EXERCISE"),
            QStringLiteral("ASSIGNMENT"),
            QStringLiteral("EXPIRATION"),
            QStringLiteral("SPLIT"),
            QStringLiteral("SPINOFF"),
            QStringLiteral("MERGER")
        });
        const bool isExternal = !isIncome && !isTrade && containsKeyword(text, {
            QStringLiteral("TRANSFER"),
            QStringLiteral("ACH"),
            QStringLiteral("WIRE"),
            QStringLiteral("EFT"),
            QStringLiteral("CHECK"),
            QStringLiteral("DEPOSIT"),
            QStringLiteral("WITHDRAW"),
            QStringLiteral("CONTRIBUTION"),
            QStringLiteral("DISTRIBUTION"),
            QStringLiteral("DISBURSEMENT"),
            QStringLiteral("JOURNAL"),
            QStringLiteral("ROLLOVER"),
            QStringLiteral("IRA"),
            QStringLiteral("ROTH")
        });

        if (!isIncome && !isExternal) {
            continue;
        }

        auto &pending = m_pendingTransactionFlows[date];
        if (isExternal) {
            pending.externalFlow += amount;
        }
        if (isIncome) {
            pending.incomeFlow += amount;
        }
    }

    if (moreTransactions && !nextMarker.isEmpty()) {
        reply->deleteLater();
        fetchTransactionsPage(startDate, endDate, nextMarker);
        return;
    }

    clearFlowRange(startDate, endDate);
    for (auto it = m_pendingTransactionFlows.cbegin(); it != m_pendingTransactionFlows.cend(); ++it) {
        auto &entry = ensureHistoryEntry(it.key());
        entry.externalFlow = it.value().externalFlow;
        entry.incomeFlow = it.value().incomeFlow;
    }

    m_lastTransactionSyncDate = endDate;
    m_pendingTransactionFlows.clear();
    saveHistory();
    updateChartPoints();

    m_retryingAfterRenew = false;
    setLoading(false);
    setStatusText(QStringLiteral("Updated %1 positions.").arg(m_positions.size()));
    setLastError({});
    reply->deleteLater();
}

void ETradeClient::loadHistory() {
    m_history.clear();
    m_pendingTransactionFlows.clear();
    m_trackingStartDate = {};
    m_trackingBaselineValue = 0.0;
    m_lastTransactionSyncDate = {};
    QFile file(historyFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        writeLog(QStringLiteral("history load empty file=%1").arg(historyFilePath()));
        updateChartPoints();
        return;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isArray()) {
        for (const auto &value : doc.array()) {
            const auto object = value.toObject();
            const auto date = QDate::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
            if (!date.isValid()) {
                continue;
            }
            Snapshot snapshot;
            snapshot.date = date;
            snapshot.hasValue = true;
            snapshot.value = readDouble(object.value(QStringLiteral("value")));
            m_history.append(snapshot);
        }
    } else if (doc.isObject()) {
        const auto root = doc.object();
        m_trackingStartDate = QDate::fromString(root.value(QStringLiteral("trackingStartDate")).toString(), Qt::ISODate);
        m_trackingBaselineValue = readDouble(root.value(QStringLiteral("trackingBaselineValue")));
        m_lastTransactionSyncDate = QDate::fromString(root.value(QStringLiteral("lastTransactionSyncDate")).toString(), Qt::ISODate);

        const auto entriesValue = root.value(QStringLiteral("entries")).isUndefined()
            ? root.value(QStringLiteral("history"))
            : root.value(QStringLiteral("entries"));
        if (entriesValue.isArray()) {
            for (const auto &value : entriesValue.toArray()) {
                const auto object = value.toObject();
                const auto date = QDate::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
                if (!date.isValid()) {
                    continue;
                }

                Snapshot snapshot;
                snapshot.date = date;
                snapshot.hasValue = object.value(QStringLiteral("hasValue")).toBool(object.contains(QStringLiteral("value")));
                snapshot.value = readDouble(object.value(QStringLiteral("value")));
                snapshot.externalFlow = readDouble(object.value(QStringLiteral("externalFlow")));
                snapshot.incomeFlow = readDouble(object.value(QStringLiteral("incomeFlow")));
                if (snapshot.hasValue || snapshot.externalFlow != 0.0 || snapshot.incomeFlow != 0.0) {
                    m_history.append(snapshot);
                }
            }
        }
    } else {
        updateChartPoints();
        return;
    }

    std::sort(m_history.begin(), m_history.end(), [](const Snapshot &left, const Snapshot &right) {
        return left.date < right.date;
    });

    if (!m_trackingStartDate.isValid() || m_trackingBaselineValue <= 0.0) {
        for (const auto &snapshot : m_history) {
            if (snapshot.hasValue && snapshot.value > 0.0) {
                m_trackingStartDate = snapshot.date;
                m_trackingBaselineValue = snapshot.value;
                break;
            }
        }
    }

    writeLog(QStringLiteral("history loaded file=%1 count=%2")
             .arg(historyFilePath(), QString::number(m_history.size())));
    updateChartPoints();
}

void ETradeClient::saveHistory() const {
    QDir().mkpath(storagePath());
    QFile file(historyFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    if (m_trackingStartDate.isValid()) {
        root.insert(QStringLiteral("trackingStartDate"), m_trackingStartDate.toString(Qt::ISODate));
    }
    root.insert(QStringLiteral("trackingBaselineValue"), m_trackingBaselineValue);
    if (m_lastTransactionSyncDate.isValid()) {
        root.insert(QStringLiteral("lastTransactionSyncDate"), m_lastTransactionSyncDate.toString(Qt::ISODate));
    }

    QJsonArray array;
    for (const auto &snapshot : m_history) {
        if (!snapshot.hasValue && snapshot.externalFlow == 0.0 && snapshot.incomeFlow == 0.0) {
            continue;
        }

        QJsonObject object;
        object.insert(QStringLiteral("date"), snapshot.date.toString(Qt::ISODate));
        object.insert(QStringLiteral("hasValue"), snapshot.hasValue);
        if (snapshot.hasValue) {
            object.insert(QStringLiteral("value"), snapshot.value);
        }
        if (snapshot.externalFlow != 0.0) {
            object.insert(QStringLiteral("externalFlow"), snapshot.externalFlow);
        }
        if (snapshot.incomeFlow != 0.0) {
            object.insert(QStringLiteral("incomeFlow"), snapshot.incomeFlow);
        }
        array.append(object);
    }
    root.insert(QStringLiteral("entries"), array);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ETradeClient::updateChartPoints() {
    QVariantList points;
    const auto minDate = m_chartMonths > 0 ? QDate::currentDate().addMonths(-m_chartMonths) : QDate();
    auto trackingStartDate = m_trackingStartDate;
    auto trackingBaselineValue = m_trackingBaselineValue;

    if (!trackingStartDate.isValid() || trackingBaselineValue <= 0.0) {
        for (const auto &snapshot : m_history) {
            if (snapshot.hasValue && snapshot.value > 0.0) {
                trackingStartDate = snapshot.date;
                trackingBaselineValue = snapshot.value;
                break;
            }
        }
    }

    double cumulativeExternalFlow = 0.0;
    double pendingExternalFlow = 0.0;
    double latestNetInvested = 0.0;
    double latestProfitLoss = 0.0;
    double latestProfitLossPct = 0.0;
    double latestDailyPerformance = 0.0;
    double latestPeakValue = 0.0;
    double latestDrawdown = 0.0;
    double latestDrawdownPct = 0.0;
    double previousValue = 0.0;
    bool hasPreviousValue = false;

    for (const auto &snapshot : m_history) {
        if (trackingStartDate.isValid() && snapshot.date > trackingStartDate) {
            cumulativeExternalFlow += snapshot.externalFlow;
        }
        if (hasPreviousValue) {
            pendingExternalFlow += snapshot.externalFlow;
        }
        if (!snapshot.hasValue) {
            continue;
        }

        latestNetInvested = trackingBaselineValue > 0.0 ? trackingBaselineValue + cumulativeExternalFlow : snapshot.value;
        latestProfitLoss = snapshot.value - latestNetInvested;
        latestProfitLossPct = std::abs(latestNetInvested) > 0.01 ? (latestProfitLoss / latestNetInvested) * 100.0 : 0.0;
        latestPeakValue = std::max(latestPeakValue, snapshot.value);
        latestDrawdown = snapshot.value - latestPeakValue;
        latestDrawdownPct = latestPeakValue > 0.01 ? (latestDrawdown / latestPeakValue) * 100.0 : 0.0;
        latestDailyPerformance = hasPreviousValue ? (snapshot.value - previousValue - pendingExternalFlow) : 0.0;

        if (m_chartMonths > 0 && snapshot.date < minDate) {
            previousValue = snapshot.value;
            pendingExternalFlow = 0.0;
            hasPreviousValue = true;
            continue;
        }

        QVariantMap point;
        point.insert(QStringLiteral("date"), snapshot.date.toString(Qt::ISODate));
        point.insert(QStringLiteral("label"), snapshot.date.toString(QStringLiteral("MMM d")));
        point.insert(QStringLiteral("value"), snapshot.value);
        point.insert(QStringLiteral("accountValue"), snapshot.value);
        point.insert(QStringLiteral("netInvested"), latestNetInvested);
        point.insert(QStringLiteral("profitLoss"), latestProfitLoss);
        point.insert(QStringLiteral("profitLossPct"), latestProfitLossPct);
        point.insert(QStringLiteral("dailyPerformance"), latestDailyPerformance);
        point.insert(QStringLiteral("externalFlow"), pendingExternalFlow);
        point.insert(QStringLiteral("incomeFlow"), snapshot.incomeFlow);
        point.insert(QStringLiteral("peakValue"), latestPeakValue);
        point.insert(QStringLiteral("drawdown"), latestDrawdown);
        point.insert(QStringLiteral("drawdownPct"), latestDrawdownPct);
        points.append(point);

        previousValue = snapshot.value;
        pendingExternalFlow = 0.0;
        hasPreviousValue = true;
    }

    m_chartPoints = points;
    m_netInvested = latestNetInvested;
    m_profitLoss = latestProfitLoss;
    m_profitLossPct = latestProfitLossPct;
    m_dailyPerformance = latestDailyPerformance;
    m_peakValue = latestPeakValue;
    m_drawdown = latestDrawdown;
    m_drawdownPct = latestDrawdownPct;
    writeLog(QStringLiteral("chart updated months=%1 visiblePoints=%2 historyCount=%3 file=%4")
             .arg(QString::number(m_chartMonths), QString::number(m_chartPoints.size()), QString::number(m_history.size()), historyFilePath()));
    emit chartPointsChanged();
    emit summaryChanged();
}

void ETradeClient::appendSnapshot(double value) {
    if (value <= 0.0) {
        return;
    }

    const auto today = QDate::currentDate();
    initializeTrackingBaseline(value, today);

    auto &snapshot = ensureHistoryEntry(today);
    snapshot.hasValue = true;
    snapshot.value = value;

    saveHistory();
    updateChartPoints();
}

ETradeClient::Snapshot &ETradeClient::ensureHistoryEntry(const QDate &date) {
    for (int index = 0; index < m_history.size(); ++index) {
        if (m_history[index].date == date) {
            return m_history[index];
        }
        if (m_history[index].date > date) {
            m_history.insert(index, Snapshot{date});
            return m_history[index];
        }
    }

    m_history.append(Snapshot{date});
    return m_history.last();
}

void ETradeClient::clearFlowRange(const QDate &startDate, const QDate &endDate) {
    for (auto &snapshot : m_history) {
        if (snapshot.date < startDate || snapshot.date > endDate) {
            continue;
        }
        snapshot.externalFlow = 0.0;
        snapshot.incomeFlow = 0.0;
    }

    m_history.erase(std::remove_if(m_history.begin(), m_history.end(), [](const Snapshot &snapshot) {
        return !snapshot.hasValue && snapshot.externalFlow == 0.0 && snapshot.incomeFlow == 0.0;
    }), m_history.end());
}

void ETradeClient::initializeTrackingBaseline(double value, const QDate &date) {
    if (m_trackingStartDate.isValid() && m_trackingBaselineValue > 0.0) {
        return;
    }
    if (!date.isValid() || value <= 0.0) {
        return;
    }

    m_trackingStartDate = date;
    m_trackingBaselineValue = value;
}

void ETradeClient::loadDemoData() {
    QVariantList demoPositions;

    auto addPosition = [&](const QString &symbol,
                           const QString &name,
                           double quantity,
                           double marketValue,
                           double totalGain,
                           double daysGain,
                           double lastTrade) {
        QVariantMap map;
        map.insert(QStringLiteral("symbol"), symbol);
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("quantity"), quantity);
        map.insert(QStringLiteral("marketValue"), marketValue);
        map.insert(QStringLiteral("totalGainLoss"), totalGain);
        map.insert(QStringLiteral("totalGainLossPct"), marketValue > totalGain ? (totalGain / (marketValue - totalGain)) * 100.0 : 0.0);
        map.insert(QStringLiteral("daysGainLoss"), daysGain);
        map.insert(QStringLiteral("daysGainLossPct"), marketValue > daysGain ? (daysGain / (marketValue - daysGain)) * 100.0 : 0.0);
        map.insert(QStringLiteral("lastTrade"), lastTrade);
        map.insert(QStringLiteral("pctOfPortfolio"), 0.0);
        demoPositions.append(map);
    };

    addPosition(QStringLiteral("AAPL"), QStringLiteral("Apple"), 28.0, 6192.40, 1410.55, 73.60, 221.16);
    addPosition(QStringLiteral("MSFT"), QStringLiteral("Microsoft"), 15.0, 7149.75, 1894.10, -21.45, 476.65);
    addPosition(QStringLiteral("VOO"), QStringLiteral("Vanguard S&P 500 ETF"), 32.0, 17855.36, 2960.32, 112.00, 557.98);
    addPosition(QStringLiteral("NVDA"), QStringLiteral("NVIDIA"), 20.0, 2944.00, 841.20, 64.20, 147.20);

    m_positions = demoPositions;
    m_positionsValue = 34141.51;
    m_cashBalance = 2188.67;
    m_totalValue = m_positionsValue + m_cashBalance;
    m_totalGainLoss = 7106.17;
    m_totalGainLossPct = 24.25;
    m_todaysGainLoss = 228.35;
    m_todaysGainLossPct = 0.63;

    m_history.clear();
    m_trackingStartDate = {};
    m_trackingBaselineValue = 0.0;
    m_lastTransactionSyncDate = {};
    const auto today = QDate::currentDate();
    for (int offset = 90; offset >= 0; --offset) {
        Snapshot snapshot;
        snapshot.date = today.addDays(-offset);
        snapshot.hasValue = true;
        const auto phase = static_cast<double>(offset) / 9.0;
        snapshot.value = 28600.0 + (90 - offset) * 72.0 + std::sin(phase) * 540.0;
        m_history.append(snapshot);
    }
    if (!m_history.isEmpty()) {
        m_trackingStartDate = m_history.first().date;
        m_trackingBaselineValue = m_history.first().value;
    }
    updateChartPoints();
    setStatusText(QStringLiteral("Loaded demo portfolio data."));
    setLastError({});
    writeLog(QStringLiteral("demo data loaded"));
    emit positionsChanged();
    emit summaryChanged();
}
