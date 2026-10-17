#include <QQmlExtensionPlugin>
#include <qqml.h>

#include "globeitem.h"
#include "particleglobeitem.h"

class EncomGlobePlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri) override
    {
        qmlRegisterType<GlobeItem>(uri, 1, 0, "Globe");
        qmlRegisterType<ParticleGlobeItem>(uri, 1, 0, "ParticleGlobe");
    }
};

#include "plugin.moc"
