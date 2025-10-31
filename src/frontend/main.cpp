#include <QApplication>

#include "backend/AppBackend.h"
#include "frontend/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    backend::AppBackend backend;
    backend.initialize("data");

    MainWindow w(backend);
    w.resize(960, 600);
    w.show();

    return app.exec();
}
