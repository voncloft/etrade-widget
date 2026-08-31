#include <QQmlExtensionPlugin>
#include <qqml.h>

#include "etradeclient.h"

class ETradePlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid FILE "etrade_plugin.json")

public:
    void registerTypes(const char *uri) override {
        qmlRegisterType<ETradeClient>(uri, 1, 0, "ETradeClient");
    }
};

#include "plugin.moc"
