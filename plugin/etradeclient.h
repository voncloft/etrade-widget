#pragma once

#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

#include <functional>

class QJsonObject;
class QJsonValue;

class QNetworkReply;

class ETradeClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool sandbox READ sandbox WRITE setSandbox NOTIFY sandboxChanged)
    Q_PROPERTY(QString consumerKey READ consumerKey WRITE setConsumerKey NOTIFY credentialsChanged)
    Q_PROPERTY(QString consumerSecret READ consumerSecret WRITE setConsumerSecret NOTIFY credentialsChanged)
    Q_PROPERTY(QString accountIdKey READ accountIdKey WRITE setAccountIdKey NOTIFY accountIdKeyChanged)
    Q_PROPERTY(QString requestToken READ requestToken NOTIFY requestTokenChanged)
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginUrlChanged)
    Q_PROPERTY(QString accessToken READ accessToken NOTIFY accessTokenChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(double totalValue READ totalValue NOTIFY summaryChanged)
    Q_PROPERTY(double positionsValue READ positionsValue NOTIFY summaryChanged)
    Q_PROPERTY(double cashBalance READ cashBalance NOTIFY summaryChanged)
    Q_PROPERTY(double totalGainLoss READ totalGainLoss NOTIFY summaryChanged)
    Q_PROPERTY(double totalGainLossPct READ totalGainLossPct NOTIFY summaryChanged)
    Q_PROPERTY(double todaysGainLoss READ todaysGainLoss NOTIFY summaryChanged)
    Q_PROPERTY(double todaysGainLossPct READ todaysGainLossPct NOTIFY summaryChanged)
    Q_PROPERTY(double netInvested READ netInvested NOTIFY summaryChanged)
    Q_PROPERTY(double profitLoss READ profitLoss NOTIFY summaryChanged)
    Q_PROPERTY(double profitLossPct READ profitLossPct NOTIFY summaryChanged)
    Q_PROPERTY(double dailyPerformance READ dailyPerformance NOTIFY summaryChanged)
    Q_PROPERTY(double peakValue READ peakValue NOTIFY summaryChanged)
    Q_PROPERTY(double drawdown READ drawdown NOTIFY summaryChanged)
    Q_PROPERTY(double drawdownPct READ drawdownPct NOTIFY summaryChanged)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY positionsChanged)
    Q_PROPERTY(QVariantList chartPoints READ chartPoints NOTIFY chartPointsChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QString selectedAccountLabel READ selectedAccountLabel NOTIFY accountsChanged)
    Q_PROPERTY(QString trackingStartLabel READ trackingStartLabel NOTIFY chartPointsChanged)
    Q_PROPERTY(int refreshMinutes READ refreshMinutes WRITE setRefreshMinutes NOTIFY refreshMinutesChanged)
    Q_PROPERTY(int chartMonths READ chartMonths WRITE setChartMonths NOTIFY chartMonthsChanged)

public:
    explicit ETradeClient(QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    bool authenticated() const;
    bool sandbox() const { return m_sandbox; }
    QString consumerKey() const { return m_consumerKey; }
    QString consumerSecret() const { return m_consumerSecret; }
    QString accountIdKey() const { return m_accountIdKey; }
    QString requestToken() const { return m_requestToken; }
    QString loginUrl() const { return m_loginUrl; }
    QString accessToken() const { return m_accessToken; }
    QString lastError() const { return m_lastError; }
    QString statusText() const { return m_statusText; }
    QString storagePath() const;
    QString logFilePath() const;
    double totalValue() const { return m_totalValue; }
    double positionsValue() const { return m_positionsValue; }
    double cashBalance() const { return m_cashBalance; }
    double totalGainLoss() const { return m_totalGainLoss; }
    double totalGainLossPct() const { return m_totalGainLossPct; }
    double todaysGainLoss() const { return m_todaysGainLoss; }
    double todaysGainLossPct() const { return m_todaysGainLossPct; }
    double netInvested() const { return m_netInvested; }
    double profitLoss() const { return m_profitLoss; }
    double profitLossPct() const { return m_profitLossPct; }
    double dailyPerformance() const { return m_dailyPerformance; }
    double peakValue() const { return m_peakValue; }
    double drawdown() const { return m_drawdown; }
    double drawdownPct() const { return m_drawdownPct; }
    QVariantList positions() const { return m_positions; }
    QVariantList chartPoints() const { return m_chartPoints; }
    QVariantList accounts() const { return m_accounts; }
    QString selectedAccountLabel() const;
    QString trackingStartLabel() const;
    int refreshMinutes() const { return m_refreshMinutes; }
    int chartMonths() const { return m_chartMonths; }

    void setSandbox(bool sandbox);
    void setConsumerKey(const QString &consumerKey);
    void setConsumerSecret(const QString &consumerSecret);
    void setAccountIdKey(const QString &accountIdKey);
    void setRefreshMinutes(int refreshMinutes);
    void setChartMonths(int chartMonths);

    Q_INVOKABLE void loadSettings();
    Q_INVOKABLE bool saveSettings();
    Q_INVOKABLE void clearSession();
    Q_INVOKABLE void startAuthorization();
    Q_INVOKABLE void completeAuthorization(const QString &verifier);
    Q_INVOKABLE void renewAccessToken();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadDemoData();

signals:
    void loadingChanged();
    void authenticatedChanged();
    void sandboxChanged();
    void credentialsChanged();
    void accountIdKeyChanged();
    void requestTokenChanged();
    void loginUrlChanged();
    void accessTokenChanged();
    void lastErrorChanged();
    void statusTextChanged();
    void summaryChanged();
    void positionsChanged();
    void chartPointsChanged();
    void accountsChanged();
    void refreshMinutesChanged();
    void chartMonthsChanged();

private:
    struct OAuthToken {
        QString token;
        QString secret;
    };

    enum class RefreshStage {
        Idle,
        ListingAccounts,
        FetchingPortfolio
    };

    struct Snapshot {
        QDate date;
        bool hasValue = false;
        double value = 0.0;
        double externalFlow = 0.0;
        double incomeFlow = 0.0;
    };

    struct PendingFlow {
        double externalFlow = 0.0;
        double incomeFlow = 0.0;
    };

    void setLoading(bool loading);
    void setLastError(const QString &lastError);
    void setStatusText(const QString &statusText);

    QString configFilePath() const;
    QString historyFilePath() const;
    QString logPath() const;
    QString apiBaseUrl() const;
    QString authorizeBaseUrl() const;

    void loadHistory();
    void saveHistory() const;
    void updateChartPoints();
    void appendSnapshot(double value);
    void fetchTransactionsPage(const QDate &startDate, const QDate &endDate, const QString &marker = {});
    void processTransactionsReply(QNetworkReply *reply, const QDate &startDate, const QDate &endDate);
    void clearFlowRange(const QDate &startDate, const QDate &endDate);
    Snapshot &ensureHistoryEntry(const QDate &date);
    void initializeTrackingBaseline(double value, const QDate &date);
    void writeLog(const QString &message) const;
    void writeReplyLog(const QString &label, QNetworkReply *reply, const QByteArray &body) const;
    bool replyLooksLikeInvalidAccessToken(QNetworkReply *reply, const QByteArray &body) const;

    QByteArray buildAuthorizationHeader(const QString &method,
                                        const QUrl &url,
                                        const QList<QPair<QString, QString>> &extraOAuth,
                                        const QList<QPair<QString, QString>> &queryParameters,
                                        const QString &tokenSecret,
                                        const QString &oauthTokenOverride = {}) const;
    QString percentEncode(const QString &value) const;
    QList<QPair<QString, QString>> normalizedParameters(const QList<QPair<QString, QString>> &oauthParameters,
                                                        const QList<QPair<QString, QString>> &queryParameters) const;
    QByteArray hmacSha1(const QByteArray &key, const QByteArray &message) const;
    OAuthToken parseTokenReply(const QByteArray &body) const;

    void sendSignedGet(const QUrl &url, const std::function<void(QNetworkReply *)> &onFinished);
    void processAccountsReply(QNetworkReply *reply);
    void processPortfolioReply(QNetworkReply *reply);
    void retryRefreshAfterRenew();

    static double readDouble(const QJsonValue &value, double fallback = 0.0);
    static QString readString(const QJsonValue &value);
    static QDate readDate(const QJsonValue &value);
    static bool containsKeyword(const QString &text, const QStringList &keywords);
    static QVariantMap positionToMap(const QJsonObject &positionObject);

    QNetworkAccessManager m_network;
    bool m_loading = false;
    bool m_sandbox = false;
    QString m_consumerKey;
    QString m_consumerSecret;
    QString m_accountIdKey;
    QString m_requestToken;
    QString m_requestTokenSecret;
    QString m_loginUrl;
    QString m_accessToken;
    QString m_accessTokenSecret;
    QString m_lastError;
    QString m_statusText;
    double m_totalValue = 0.0;
    double m_positionsValue = 0.0;
    double m_cashBalance = 0.0;
    double m_totalGainLoss = 0.0;
    double m_totalGainLossPct = 0.0;
    double m_todaysGainLoss = 0.0;
    double m_todaysGainLossPct = 0.0;
    double m_netInvested = 0.0;
    double m_profitLoss = 0.0;
    double m_profitLossPct = 0.0;
    double m_dailyPerformance = 0.0;
    double m_peakValue = 0.0;
    double m_drawdown = 0.0;
    double m_drawdownPct = 0.0;
    QVariantList m_positions;
    QVariantList m_chartPoints;
    QVariantList m_accounts;
    QList<Snapshot> m_history;
    QMap<QDate, PendingFlow> m_pendingTransactionFlows;
    QDate m_trackingStartDate;
    double m_trackingBaselineValue = 0.0;
    QDate m_lastTransactionSyncDate;
    int m_refreshMinutes = 15;
    int m_chartMonths = 3;
    RefreshStage m_refreshStage = RefreshStage::Idle;
    bool m_retryingAfterRenew = false;
};
