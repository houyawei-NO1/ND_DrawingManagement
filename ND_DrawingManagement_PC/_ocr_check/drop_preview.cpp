#include "ocrimagedrop.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget host;
    host.setFixedSize(280, 60);
    host.setStyleSheet(QStringLiteral(
        "QWidget { background:#2B3046; color:#ffffff; font-size:14px; }"));
    auto *lay = new QVBoxLayout(&host);
    lay->setContentsMargins(12, 12, 12, 12);
    auto *drop = new OcrImageDrop(&host);
    lay->addWidget(drop, 0, Qt::AlignLeft);
    host.show();
    app.processEvents();
    host.grab().save(QStringLiteral("drop_preview.png"));
    return 0;
}
