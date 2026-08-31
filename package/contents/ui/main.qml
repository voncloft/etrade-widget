import QtQuick 2.15
import QtQuick.Layouts 1.15

import org.kde.plasma.components 3.0 as PC3
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasmoid 2.0

import org.von.etrade 1.0

PlasmoidItem {
    id: root
    width: 780
    height: 560

    property string verifierCode: ""

    function money(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 2)
    }

    function signedMoney(value) {
        const number = Number(value)
        const prefix = number >= 0 ? "+" : "-"
        return prefix + "$" + money(Math.abs(number))
    }

    function signedPercent(value) {
        const number = Number(value)
        const prefix = number >= 0 ? "+" : "-"
        return prefix + Math.abs(number).toLocaleString(Qt.locale(), "f", 2) + "%"
    }

    function metricColor(value) {
        return Number(value) >= 0 ? "#4ade80" : "#f87171"
    }

    function selectedAccountIndex() {
        for (let i = 0; i < client.accounts.length; ++i) {
            if (client.accounts[i].accountIdKey === client.accountIdKey)
                return i
        }
        return 0
    }

    function pointNumber(point, key) {
        if (!point || point[key] === undefined)
            return 0
        return Number(point[key])
    }

    function midpointLabel() {
        if (!client.chartPoints.length)
            return ""
        return client.chartPoints[Math.floor((client.chartPoints.length - 1) / 2)].label
    }

    function chartRangeText() {
        return client.chartMonths === 0 ? "All" : client.chartMonths + "M"
    }

    Component.onCompleted: client.loadSettings()

    ETradeClient {
        id: client
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            PC3.Label {
                text: "E*TRADE Portfolio"
                font.bold: true
                font.pixelSize: 24
            }

            Item { Layout.fillWidth: true }

            PC3.Label {
                text: client.sandbox ? "Sandbox" : "Live"
                font.bold: true
                color: client.sandbox ? "#f59e0b" : "#4ade80"
            }
        }

        PC3.Label {
            Layout.fillWidth: true
            visible: client.statusText.length > 0
            text: client.statusText
            wrapMode: Text.WordWrap
            opacity: 0.86
        }

        PC3.Label {
            Layout.fillWidth: true
            visible: client.lastError.length > 0
            text: client.lastError
            wrapMode: Text.WordWrap
            color: "#f87171"
        }

        PC3.TabBar {
            id: tabs
            Layout.fillWidth: true

            PC3.TabButton { text: "Portfolio" }
            PC3.TabButton { text: "Login / Settings" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    spacing: 12

                    ColumnLayout {
                        Layout.preferredWidth: 300
                        Layout.fillHeight: true
                        spacing: 10

                        PC3.Frame {
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                PC3.Label {
                                    text: "Portfolio Controls"
                                    font.bold: true
                                }

                                PC3.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    opacity: 0.82
                                    text: client.sandbox
                                          ? "Sandbox mode shows E*TRADE sample data, not your real money. Switch to Live for actual balances."
                                          : "Live mode shows your real E*TRADE data."
                                }

                                PC3.Button {
                                    Layout.fillWidth: true
                                    text: client.loading ? "Refreshing..." : "Refresh"
                                    enabled: !client.loading
                                    onClicked: client.refresh()
                                }
                            }
                        }

                        PC3.Frame {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                PC3.Label {
                                    text: "Account"
                                    font.bold: true
                                }

                                PC3.ComboBox {
                                    Layout.fillWidth: true
                                    model: client.accounts
                                    textRole: "label"
                                    currentIndex: selectedAccountIndex()
                                    onActivated: {
                                        if (currentIndex >= 0 && currentIndex < client.accounts.length)
                                            client.accountIdKey = client.accounts[currentIndex].accountIdKey
                                    }
                                }

                                PC3.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: client.accountIdKey.length > 0 ? "Selected: " + client.selectedAccountLabel : "No account selected yet"
                                }

                                PC3.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    opacity: 0.78
                                    text: client.accounts.length > 0
                                          ? "Choose the account whose holdings you want on the main dashboard."
                                          : "No accounts loaded yet. Refresh after finishing login."
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: 10
                            columnSpacing: 10

                            Repeater {
                                model: [
                                    { title: "Account value", value: "$" + money(client.totalValue), detail: "Latest account snapshot" },
                                    { title: "Total gain/loss", value: signedMoney(client.totalGainLoss), detail: signedPercent(client.totalGainLossPct), tone: metricColor(client.totalGainLoss) },
                                    { title: "Today", value: signedMoney(client.todaysGainLoss), detail: signedPercent(client.todaysGainLossPct), tone: metricColor(client.todaysGainLoss) },
                                    { title: "Invested in holdings", value: "$" + money(client.positionsValue), detail: client.positions.length + " positions" },
                                    { title: "Cash balance", value: "$" + money(client.cashBalance), detail: "Available cash now" },
                                    { title: "Refresh state", value: client.loading ? "Updating..." : "Current", detail: client.loading ? "Pulling latest E*TRADE data" : "Cards show the latest refresh" }
                                ]

                                delegate: PC3.Frame {
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 4

                                        PC3.Label {
                                            text: modelData.title
                                            opacity: 0.8
                                        }

                                        PC3.Label {
                                            text: modelData.value
                                            font.pixelSize: 22
                                            font.bold: true
                                            color: modelData.tone ? modelData.tone : PlasmaCore.Theme.textColor
                                        }

                                        PC3.Label {
                                            text: modelData.detail
                                            opacity: 0.82
                                        }
                                    }
                                }
                            }
                        }

                        PC3.TabBar {
                            id: portfolioViews
                            Layout.fillWidth: true

                            PC3.TabButton { text: "Holdings" }
                            PC3.TabButton { text: "Graph" }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: portfolioViews.currentIndex === 0 ? 1 : 0

                            PC3.Frame {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true

                                        PC3.Label {
                                            text: "Forward-tracked performance"
                                            font.bold: true
                                        }

                                        Item { Layout.fillWidth: true }

                                        PC3.Label {
                                            text: client.chartPoints.length > 0
                                                  ? chartRangeText() + " · " + client.chartPoints[0].label + " → " + client.chartPoints[client.chartPoints.length - 1].label
                                                  : "No history yet"
                                            opacity: 0.8
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Repeater {
                                            model: [
                                                { label: "1M", months: 1 },
                                                { label: "3M", months: 3 },
                                                { label: "6M", months: 6 },
                                                { label: "12M", months: 12 },
                                                { label: "All", months: 0 }
                                            ]

                                            delegate: PC3.Button {
                                                text: modelData.label
                                                checkable: true
                                                checked: client.chartMonths === modelData.months
                                                onClicked: client.chartMonths = modelData.months
                                            }
                                        }
                                    }

                                    PC3.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        opacity: 0.74
                                        text: client.trackingStartLabel.length > 0
                                              ? "Top cards show the latest live snapshot. This graph stays overall from " + client.trackingStartLabel + " forward."
                                              : "Top cards show the latest live snapshot. The graph starts on the first tracked day and uses real dates only."
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 14

                                        RowLayout {
                                            spacing: 6
                                            Rectangle { width: 12; height: 12; radius: 2; color: "#4ade80" }
                                            PC3.Label { text: "Value"; opacity: 0.82 }
                                        }

                                        RowLayout {
                                            spacing: 6
                                            Rectangle { width: 12; height: 12; radius: 2; color: "#fbbf24" }
                                            PC3.Label { text: "Invested"; opacity: 0.82 }
                                        }

                                        RowLayout {
                                            spacing: 6
                                            Rectangle { width: 12; height: 12; radius: 2; color: "#22c55e" }
                                            PC3.Label { text: "Profit band"; opacity: 0.82 }
                                        }

                                        RowLayout {
                                            spacing: 6
                                            Rectangle { width: 12; height: 12; radius: 2; color: "#fb7185" }
                                            PC3.Label { text: "Drawdown"; opacity: 0.82 }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        PC3.Frame {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                anchors.fill: parent
                                                spacing: 2

                                                PC3.Label { text: "Net invested"; opacity: 0.8 }
                                                PC3.Label { text: "$" + money(client.netInvested); font.bold: true }
                                            }
                                        }

                                        PC3.Frame {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                anchors.fill: parent
                                                spacing: 2

                                                PC3.Label { text: "Tracking P/L"; opacity: 0.8 }
                                                PC3.Label {
                                                    text: signedMoney(client.profitLoss)
                                                    font.bold: true
                                                    color: metricColor(client.profitLoss)
                                                }
                                            }
                                        }

                                        PC3.Frame {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                anchors.fill: parent
                                                spacing: 2

                                                PC3.Label { text: "Drawdown"; opacity: 0.8 }
                                                PC3.Label {
                                                    text: signedMoney(client.drawdown)
                                                    font.bold: true
                                                    color: metricColor(client.drawdown)
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        PerformanceChart {
                                            anchors.fill: parent
                                            points: client.chartPoints
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        PC3.Label {
                                            text: client.chartPoints.length > 0 ? client.chartPoints[0].label : ""
                                            opacity: 0.72
                                        }

                                        Item { Layout.fillWidth: true }

                                        PC3.Label {
                                            text: midpointLabel()
                                            opacity: 0.72
                                        }

                                        Item { Layout.fillWidth: true }

                                        PC3.Label {
                                            text: client.chartPoints.length > 0 ? client.chartPoints[client.chartPoints.length - 1].label : ""
                                            opacity: 0.72
                                        }
                                    }
                                }
                            }

                            PC3.Frame {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8

                                    PC3.Label {
                                        text: "Holdings"
                                        font.bold: true
                                    }

                                    PC3.ScrollView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        ListView {
                                            id: holdingsView
                                            model: client.positions
                                            spacing: 6
                                            clip: true

                                            delegate: PC3.ItemDelegate {
                                                width: holdingsView.width

                                                contentItem: ColumnLayout {
                                                    spacing: 4

                                                    RowLayout {
                                                        spacing: 8

                                                        PC3.Label {
                                                            text: modelData.symbol
                                                            font.bold: true
                                                        }

                                                        PC3.Label {
                                                            Layout.fillWidth: true
                                                            text: modelData.name
                                                            elide: Text.ElideRight
                                                            opacity: 0.82
                                                        }

                                                        PC3.Label {
                                                            text: "$" + money(modelData.marketValue)
                                                            font.bold: true
                                                        }
                                                    }

                                                    RowLayout {
                                                        spacing: 10

                                                        PC3.Label { text: "Qty " + Number(modelData.quantity).toLocaleString(Qt.locale(), "f", 2) }
                                                        PC3.Label { text: "Last $" + money(modelData.lastTrade) }
                                                        PC3.Label {
                                                            text: "Day " + signedMoney(modelData.daysGainLoss) + " (" + signedPercent(modelData.daysGainLossPct) + ")"
                                                            color: metricColor(modelData.daysGainLoss)
                                                        }
                                                        PC3.Label {
                                                            text: "Total " + signedMoney(modelData.totalGainLoss) + " (" + signedPercent(modelData.totalGainLossPct) + ")"
                                                            color: metricColor(modelData.totalGainLoss)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    PC3.Frame {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            PC3.Label {
                                text: "Login and Settings"
                                font.bold: true
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                PC3.Label { text: "Mode" }
                                PC3.ComboBox {
                                    Layout.fillWidth: true
                                    model: ["Sandbox", "Live"]
                                    currentIndex: client.sandbox ? 0 : 1
                                    onActivated: client.sandbox = (currentIndex === 0)
                                }
                            }

                            PC3.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.82
                                text: client.sandbox
                                      ? "Sandbox keys only work in Sandbox mode. E*TRADE sandbox returns stored sample data, not your real balances or holdings. Request tokens also expire after about 5 minutes."
                                      : "Live mode uses your real E*TRADE account data. Request tokens expire after about 5 minutes, and access tokens can expire or need renewal later in the day."
                            }

                            PC3.TextField {
                                Layout.fillWidth: true
                                placeholderText: "Consumer key"
                                text: client.consumerKey
                                onTextEdited: client.consumerKey = text
                            }

                            PC3.TextField {
                                Layout.fillWidth: true
                                placeholderText: "Consumer secret"
                                echoMode: TextInput.Password
                                text: client.consumerSecret
                                onTextEdited: client.consumerSecret = text
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                PC3.Label { text: "Refresh" }
                                PC3.SpinBox {
                                    id: refreshSpin
                                    from: 1
                                    to: 240
                                    value: client.refreshMinutes
                                    onValueModified: client.refreshMinutes = value
                                }
                                PC3.Label { text: "min" }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                PC3.Label { text: "Chart" }
                                PC3.SpinBox {
                                    id: chartSpin
                                    from: 1
                                    to: 12
                                    value: client.chartMonths
                                    onValueModified: client.chartMonths = value
                                }
                                PC3.Label { text: "months" }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                PC3.Button {
                                    Layout.fillWidth: true
                                    text: "Save Settings"
                                    onClicked: client.saveSettings()
                                }

                                PC3.Button {
                                    Layout.fillWidth: true
                                    text: "Renew Access Token"
                                    enabled: !client.loading && client.authenticated
                                    onClicked: client.renewAccessToken()
                                }
                            }

                            PC3.Button {
                                Layout.fillWidth: true
                                text: "1. Start E*TRADE Login"
                                enabled: !client.loading
                                onClicked: client.startAuthorization()
                            }

                            PC3.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.82
                                text: client.requestToken.length > 0
                                      ? "Request token is ready. Open the E*TRADE page, approve access, then paste the verifier code below."
                                      : "Step 1 creates a temporary request token. If the widget restarts or a few minutes pass, click Start E*TRADE Login again."
                            }

                            PC3.Button {
                                Layout.fillWidth: true
                                text: "2. Open Authorization Page"
                                enabled: client.loginUrl.length > 0
                                onClicked: {
                                    if (client.loginUrl.length > 0)
                                        Qt.openUrlExternally(client.loginUrl)
                                }
                            }

                            PC3.TextField {
                                Layout.fillWidth: true
                                enabled: client.requestToken.length > 0
                                placeholderText: client.requestToken.length > 0 ? "Paste verifier code" : "Click 1. Start E*TRADE Login first"
                                text: root.verifierCode
                                onTextEdited: root.verifierCode = text
                            }

                            PC3.Button {
                                Layout.fillWidth: true
                                text: "3. Finish Login"
                                enabled: !client.loading && client.requestToken.length > 0 && root.verifierCode.length > 0
                                onClicked: client.completeAuthorization(root.verifierCode)
                            }

                            PC3.Button {
                                Layout.fillWidth: true
                                text: "Clear Tokens"
                                onClicked: {
                                    root.verifierCode = ""
                                    client.clearSession()
                                }
                            }

                            PC3.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.82
                                text: "Config is stored locally at " + client.storagePath + ". Debug logs are appended to " + client.logFilePath + ". The chart is built from daily snapshots saved by the widget, so the multi-month graph fills in as you use it."
                            }

                            PC3.TextField {
                                Layout.fillWidth: true
                                readOnly: true
                                text: client.logFilePath
                            }
                        }
                    }
                }
            }
        }
    }
}
