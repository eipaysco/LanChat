#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>
#include <QStringList>

namespace {

QString profileFromArguments(const QStringList& args) {
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == "--profile" && i + 1 < args.size()) return args.at(i + 1);
        if (arg.startsWith("--profile=")) return arg.mid(QString("--profile=").size());
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LanChat");
    QApplication::setOrganizationName("LanChat");

    const QString profile = profileFromArguments(app.arguments());
    if (!profile.isEmpty()) Settings::setProfileName(profile);

    MainWindow w;
    if (!Settings::profileName().isEmpty())
        w.setWindowTitle(QString("LanChat [%1]").arg(Settings::profileName()));
    w.show();
    return app.exec();
}
