#include <QDebug>
#include <QPointer>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <stdexcept>
#include "albert/util/standarditem.h"
#include "xdg/iconlookup.h"
#include "configwidget.h"
#include "extension.h"

using namespace Core;
using namespace std;

#define MAX_PROPERTY_VALUE_LEN 4096
#define FALLBACK_ICON "preferences-system"

class XWindowSwitcher::Private {
    public:
        QPointer<ConfigWidget> widget;
        Display *display;
        QMap<QString, QString> iconPaths;
        QMap<QString, QString> index;
        QString fallbackIconPath;

        QFileSystemWatcher watcher;
        QFutureWatcher<QMap<QString, QString>> futureWatcher;
        bool rerun = false;

        void startIndexing();
        void finishIndexing();
        QMap<QString, QString> indexApplications() const;
};

void XWindowSwitcher::Private::startIndexing() {
    // Never run concurrent
    if(futureWatcher.future().isRunning()) {
        rerun = true;
        return;
    }

    // Run finishIndexing when the indexing thread finished
    futureWatcher.disconnect();
    QObject::connect(&futureWatcher, &QFutureWatcher<QMap<QString, QString>>::finished,
        std::bind(&Private::finishIndexing, this));

    // Run the indexer thread
    futureWatcher.setFuture(QtConcurrent::run(this, &Private::indexApplications));
}

void XWindowSwitcher::Private::finishIndexing() {
    // Get the thread results
    index = futureWatcher.future().result();

    // Rebuild
    iconPaths.clear();
    for(auto key : index.keys()) {
        iconPaths.insert(key, index.value(key));
    }

    // Finally update the watches (maybe folders changed)
    if (!watcher.directories().isEmpty()) {
        watcher.removePaths(watcher.directories());
    } 
    QStringList xdgAppDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for(const QString &path : xdgAppDirs) {
        if(QFile::exists(path)) {
            watcher.addPath(path);
            QDirIterator dit(path, QDir::Dirs|QDir::NoDotAndDotDot);
            while (dit.hasNext()) {
                watcher.addPath(dit.next());
            }
        }
    }

    if (rerun) {
        startIndexing();
        rerun = false;
    }
}

QMap<QString, QString> XWindowSwitcher::Private::indexApplications() const {
    QMap<QString, QString> retrievedIconPaths;
    QStringList xdg_current_desktop = QString(getenv("XDG_CURRENT_DESKTOP")).split(':',QString::SkipEmptyParts);
    QLocale loc;
    QStringList xdgAppDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    /*
     * Create a list of desktop files to index (unique ids)
     * To determine the ID of a desktop file, make its full path relative to
     * the $XDG_DATA_DIRS component in which the desktop file is installed,
     * remove the "applications/" prefix, and turn '/' into '-'.
     */

    map<QString /*desktop file id*/, QString /*path*/> desktopFiles;
    for (const QString &dir : xdgAppDirs ) {
        QDirIterator fIt(dir, QStringList("*.desktop"), QDir::Files,
                         QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while(!fIt.next().isEmpty()) {
            QString desktopFileId = fIt.filePath().remove(QRegularExpression("^.*applications/")).replace("/", "-");
            desktopFiles.emplace(desktopFileId, fIt.filePath());
        }
    }

    // Iterate over all desktop files
    for (const auto &id_path_pair : desktopFiles) {
        const QString &path = id_path_pair.second;

        QString executable;
        QString iconPath;
        QString startupWMClass;
        bool desktopEntry = false;
        bool applicationType = false;
        bool noDisplay = false;

        /*
         * Get the data from the desktop file
         */

        // Read the file into a map
        {
            QFile file(path);
            if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) 
                continue;
            QTextStream stream(&file);
            for(QString line = stream.readLine(); !line.isNull(); line = stream.readLine()) {
                line = line.trimmed();

                if(!desktopEntry && line.contains("Desktop Entry")) {
                    desktopEntry = true;
                }

                if(!applicationType && line.startsWith("Type=Application")) {
                    applicationType = true;
                }

                if(!noDisplay && line.startsWith("NoDisplay=true")) {
                    noDisplay = true;
                }

                if(line.startsWith("Exec")) {
                    int index = line.indexOf("=");
                    if(index != -1) {
                        executable = line.mid(index + 1);
                    }

                } else if(line.startsWith("Icon")) {
                    int index = line.indexOf("=");
                    if(index != -1) {
                        iconPath = line.mid(index + 1);
                    }

                } else if(line.startsWith("StartupWMClass")) {
                    int index = line.indexOf("=");
                    if(index != -1) {
                        startupWMClass = line.mid(index + 1);
                    }
                }
            }
            file.close();
        }

        if(!desktopEntry || !applicationType || noDisplay) {
            continue;
        }

        if(!executable.isNull() && !iconPath.isNull()) {

            if(startupWMClass.isEmpty()) {

                int lastSlashIndex = executable.lastIndexOf("/");
                int index = executable.indexOf(' ', lastSlashIndex + 1);
                if(index != -1) {
                    executable = executable.mid(0, index);
                }

                index = executable.lastIndexOf("/");
                if(index != -1) {
                    executable = executable.mid(index + 1);
                }

                index = executable.indexOf("\"");
                while(index != -1) {
                    executable = executable.replace(index, 2, ""); 
                    index = executable.indexOf("\"");
                }

            } else {
                executable = startupWMClass.toLower();
            }

            if(!iconPath.contains("/")) {
                iconPath = XDG::IconLookup::iconPath(iconPath);
                if(iconPath.isNull()) {
                    iconPath = fallbackIconPath;
                }
            }

            if(!executable.isEmpty() && !iconPath.isEmpty()) {
                retrievedIconPaths.insert(executable, iconPath);
            }
        }
    }

    return retrievedIconPaths;
}


/** ***************************************************************************/
XWindowSwitcher::Extension::Extension() : Core::Extension("org.albert.extension.xwindowswitcher"), Core::QueryHandler(Core::Plugin::id()), d(new Private) {
    registerQueryHandler(this);

    d->fallbackIconPath = XDG::IconLookup::iconPath(FALLBACK_ICON);

    d->display = XOpenDisplay(NULL);
    if(d->display == NULL) {
        qDebug() << "Cannot open display";

    } else {

        // If the filesystem changed, trigger the scan
        connect(&d->watcher, &QFileSystemWatcher::directoryChanged, std::bind(&Private::startIndexing, d.get()));
        d->startIndexing();
    }
}



/** ***************************************************************************/
XWindowSwitcher::Extension::~Extension() {
    if(d->display != NULL) {
        XCloseDisplay(d->display);
    }
}



/** ***************************************************************************/
QWidget *XWindowSwitcher::Extension::widget(QWidget *parent) {
    if(d->widget.isNull()) {
        d->widget = new ConfigWidget(parent);
    }
    return d->widget;
}



/** ***************************************************************************/
void XWindowSwitcher::Extension::setupSession() {

}



/** ***************************************************************************/
void XWindowSwitcher::Extension::teardownSession() {

}



/** ***************************************************************************/
void XWindowSwitcher::Extension::handleQuery(Core::Query *query) const {

    if(d->display != NULL) {

        if(query->string().isEmpty() || query->string().length() <= 1) {
            return;
        }

        Window *clientList;
        unsigned long clientListSize;

        if((clientList = getClientList(d->display, &clientListSize)) == NULL) {
            qDebug() << "No windows found";
            return; 
        }

        clientListSize /= sizeof(Window);
        for(long unsigned int i = 0; i < clientListSize; i++) {
            char *title_utf8 = get_window_title(d->display, clientList[i]);
            QString windowTitle(title_utf8);

            if(title_utf8 != NULL) {
                free(title_utf8);
            } else {
                windowTitle = "";
            }

            XClassHint classHint;
            XGetClassHint(d->display, clientList[i], &classHint);
            QString applicationName(classHint.res_name);
            if(applicationName.toLower().contains(query->string().toLower()) || windowTitle.toLower().contains(query->string().toLower())) {
                auto item = make_shared<StandardItem>(applicationName);
                item->setText("Switch Windows");
                item->setSubtext(windowTitle);

                QString iconPath;
                if(d->iconPaths.contains(applicationName.toLower())) {
                    iconPath = d->iconPaths[applicationName];
                } else {
                    iconPath = XDG::IconLookup::iconPath(applicationName);

                    if(iconPath.isEmpty()) {
                        d->iconPaths.insert(applicationName.toLower(), d->fallbackIconPath);
                    } else {
                        d->iconPaths.insert(applicationName.toLower(), iconPath);
                    }
                }

                item->setIconPath(iconPath);
                item->addAction(make_shared<ActivateWindowAction>(applicationName, d->display, clientList[i]));
                query->addMatch(std::move(item), 0);
            }
        }

        if(clientList != NULL) {
            free(clientList);
        }
    }
}

Window * XWindowSwitcher::Extension::getClientList(Display *display, unsigned long *size) const {
    Window *clientList;
    char netClientList[] = "_NET_CLIENT_LIST";

    if ((clientList = (Window *)get_property(display, DefaultRootWindow(display), 
                    XA_WINDOW, netClientList, size)) == NULL) {
        return NULL;
    }

    return clientList;
}

char * XWindowSwitcher::Extension::get_property(Display *disp, Window win,
        Atom xa_prop_type, char *prop_name, unsigned long *size) const {
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    char *ret;
    
    xa_prop_name = XInternAtom(disp, prop_name, False);
    
    /* MAX_PROPERTY_VALUE_LEN / 4 explanation (XGetWindowProperty manpage):
     *
     * long_length = Specifies the length in 32-bit multiples of the
     *               data to be retrieved.
     */
    if(XGetWindowProperty(disp, win, xa_prop_name, 0, MAX_PROPERTY_VALUE_LEN / 4, False,
            xa_prop_type, &xa_ret_type, &ret_format,     
            &ret_nitems, &ret_bytes_after, &ret_prop) != Success) {
        return NULL;
    }
  
    if(xa_ret_type != xa_prop_type) {
        XFree(ret_prop);
        return NULL;
    }

    // null terminate the result to make string handling easier 
    tmp_size = (ret_format / 8) * ret_nitems;
    // Correct 64 Architecture implementation of 32 bit data
    if(ret_format==32) {
        tmp_size *= sizeof(long) / 4;
    }
    ret = (char *)malloc(tmp_size + 1);

    if(ret == NULL) {
        return NULL;
    }

    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if(size) {
        *size = tmp_size;
    }
    
    XFree(ret_prop);
    return ret;
}

char * XWindowSwitcher::Extension::get_window_title(Display *disp, Window win) const {
    char *title;
    char *net_wm_name;
    char utf8[] = "UTF8_STRING";
    char netWMName[] = "_NET_WM_NAME";
  
    net_wm_name = get_property(disp, win, XInternAtom(disp, utf8, False), netWMName, NULL);

    if (net_wm_name) {
        title = strdup(net_wm_name);
        free(net_wm_name);
        return title;

    } else {
        XTextProperty textProperty;
        if(XGetWMName(disp, win, &textProperty)) {
            char *converted = reinterpret_cast<char*>(textProperty.value);
            title = strdup(converted);
            return title;
        } else {
            return NULL;
        }
    }
}

XWindowSwitcher::ActivateWindowAction::ActivateWindowAction(const QString &text, Display *display, Window window)
    : StandardActionBase(text), display(display), window(window) {

}

void XWindowSwitcher::ActivateWindowAction::activate() const {
    if(display != NULL) {
        char netActiveWindow[] = "_NET_ACTIVE_WINDOW";
        client_msg(netActiveWindow);
        XMapRaised(display, window);
        XSetInputFocus(display, window, RevertToNone, CurrentTime);
        XSync(display, false);
    }
}

void XWindowSwitcher::ActivateWindowAction::client_msg(char *msg) const {
    XEvent event;
    long mask = SubstructureRedirectMask | SubstructureNotifyMask;

    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = XInternAtom(display, msg, False);
    event.xclient.window = window;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 0;
    event.xclient.data.l[1] = 0;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;

    XSendEvent(display, DefaultRootWindow(display), False, mask, &event);
}