// MIT License - see LICENSE
// Firmware updates with Nt Device on Zephyr. The board runs under a watchdog
// that the event loop feeds. A new image runs on trial until it has reached the
// update server, and confirms itself then; if it hangs or cannot reach the
// network, the watchdog or a reset sends MCUboot back to the old image. It
// downloads the image at kImageUrl into the second slot and reboots into it if
// its version is newer than the running one.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtDevice/QFirmwareUpdate>
#include <QtDevice/QWatchdog>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace {
// Where a newer build's zephyr.signed.bin is served, for example with python3
// -m http.server.
const char kImageUrl[] = "http://192.168.1.10:8000/zephyr.signed.bin";
} // namespace

int main() {
  int argc = 1;
  char name[] = "update";
  char *argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  QWatchdog watchdog;
  watchdog.setTimeout(10000);
  watchdog.setAutoFeed(true);
  if (watchdog.causedLastReset())
    qWarning() << "the watchdog reset the board";
  if (!watchdog.start())
    qWarning() << "no watchdog:" << watchdog.errorString();

  const QVersionNumber running = QFirmwareUpdate::runningVersion();
  const bool onTrial = !QFirmwareUpdate::isRunningImageConfirmed();
  qInfo() << "running firmware" << running
          << (onTrial ? "on trial" : "confirmed");

  QNetworkAccessManager manager;
  QFirmwareUpdate update;
  QNetworkReply *reply = nullptr;
  QObject::connect(&update, &QFirmwareUpdate::errorOccurred,
                   [&update](QFirmwareUpdate::UpdateError) {
                     qWarning() << "update:" << update.errorString();
                   });

  const auto check = [&] {
    reply = manager.get(QNetworkRequest(QUrl(QString::fromLatin1(kImageUrl))));
    QObject::connect(reply, &QNetworkReply::metaDataChanged, [&] {
      update.begin(
          reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
    });
    QObject::connect(reply, &QIODevice::readyRead,
                     [&] { update.writeFrom(reply); });
    QObject::connect(reply, &QNetworkReply::finished, [&] {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "download failed:" << reply->errorString();
        update.abort();
        return;
      }
      // The server answered, so the network works: a new image has passed its
      // test.
      if (onTrial && QFirmwareUpdate::confirmRunningImage())
        qInfo() << "confirmed the running image";
      update.writeFrom(reply);
      if (!update.finish())
        return;
      qInfo() << "downloaded firmware" << update.imageVersion();
      if (update.imageVersion() <= running) {
        qInfo() << "not newer; staying on" << running;
        update.abort();
        return;
      }
      if (update.requestUpgrade()) {
        qInfo() << "rebooting into" << update.imageVersion();
        QTimer::singleShot(500, [] { QFirmwareUpdate::reboot(); });
      }
    });
  };
  QTimer::singleShot(5000, check); // after DHCP
  return app.exec();
}
