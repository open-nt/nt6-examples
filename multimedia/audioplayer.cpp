// MIT License - see LICENSE
//
// Plays a WAV file with QMediaPlayer, printing its position, or plays it as a sound effect a few
// times with QSoundEffect.
//
//   audioplayer music.wav               play it once
//   audioplayer --effect click.wav 3    play it three times as a sound effect

#include <QtCore>
#include <QtMultimedia>

#include <cstdio>

namespace {

int playEffect(const QString &file, int times) {
    auto *effect = new QSoundEffect(qApp);
    effect->setSource(QUrl::fromLocalFile(file));
    effect->setLoopCount(times);
    effect->setVolume(0.8f);
    QObject::connect(effect, &QSoundEffect::playingChanged, effect, [effect] {
        if (!effect->isPlaying())
            QCoreApplication::quit();
    });
    effect->play();
    if (effect->status() != QSoundEffect::Ready || !effect->isPlaying()) {
        std::printf("cannot play %s\n", qPrintable(file));
        return 1;
    }
    return QCoreApplication::exec();
}

int playFile(const QString &file) {
    auto *player = new QMediaPlayer(qApp);
    auto *output = new QAudioOutput(qApp);
    output->setVolume(0.5f);
    player->setAudioOutput(output);
    QObject::connect(player, &QMediaPlayer::positionChanged, [player](qint64 position) {
        std::printf("\r%lld / %lld ms", static_cast<long long>(position), static_cast<long long>(player->duration()));
        std::fflush(stdout);
    });
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            QCoreApplication::quit();
    });
    player->setSource(QUrl::fromLocalFile(file));
    player->play();
    if (player->error() != QMediaPlayer::NoError) {
        std::printf("cannot play %s: %s\n", qPrintable(file), qPrintable(player->errorString()));
        return 1;
    }
    const int result = QCoreApplication::exec();
    std::printf("\n");
    return result;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.size() >= 2 && args.first() == QLatin1String("--effect"))
        return playEffect(args.at(1), args.size() > 2 ? args.at(2).toInt() : 1);
    if (args.size() == 1)
        return playFile(args.first());
    std::printf("usage: audioplayer file.wav | audioplayer --effect file.wav [times]\n");
    return 2;
}
