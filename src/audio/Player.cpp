#include "Player.h"
#include "PlaybackEngine.h"
#include "core/LibraryStore.h"
#include "core/Paths.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <algorithm>

Player::Player(LibraryStore *library, PlaybackEngine *engine, QObject *parent)
    : QObject(parent), m_library(library), m_engine(engine) {
    connect(engine, &PlaybackEngine::trackChanged, this, &Player::onEngineTrackChanged);
    connect(engine, &PlaybackEngine::finished, this, &Player::onEngineFinished);
    connect(engine, &PlaybackEngine::error, this, &Player::onEngineError);
    connect(engine, &PlaybackEngine::positionChanged, this, &Player::positionChanged);
    connect(engine, &PlaybackEngine::stateChanged, this, &Player::stateChanged);
    connect(engine, &PlaybackEngine::deviceRestartRequested, this, [this](qint64, double at, bool paused) {
        if (m_index) load(*m_index, !paused, at);
    });
    engine->setNextProvider([this](qint64 tail) -> std::optional<std::pair<QString, qint64>> {
        const auto idx = indexForToken(tail);
        if (!idx) return std::nullopt;
        const auto next = successor(*idx, m_queue.size(), m_repeat, true);
        if (!next) return std::nullopt;
        return std::make_pair(pathOf(m_queue[*next]), tokenFor(*next));
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Player::saveNow);
    m_periodicSave.setInterval(10'000);
    connect(&m_periodicSave, &QTimer::timeout, this, [this] { if (isPlaying()) saveNow(); });
    m_periodicSave.start();
    applyVolume();
}

// ---------- helpers ----------

std::optional<int> Player::successor(int index, int count, RepeatMode mode, bool automatic) {
    if (count <= 0) return std::nullopt;
    if (automatic && mode == RepeatMode::One) return index;
    if (index + 1 < count) return index + 1;
    if (mode == RepeatMode::All) return 0;
    return std::nullopt;
}

QString Player::pathOf(const QUuid &id) const {
    const Track *t = m_library->track(id);
    return t ? t->path : QString();
}

std::optional<QUuid> Player::currentTrackID() const {
    if (!m_index || *m_index < 0 || *m_index >= m_queue.size()) return std::nullopt;
    return m_queue[*m_index];
}
const Track *Player::currentTrack() const {
    const auto id = currentTrackID();
    return id ? m_library->track(*id) : nullptr;
}

bool Player::isPlaying() const { return m_loaded && m_engine->isPlaying(); }
QString Player::currentTrackIdString() const { const auto id = currentTrackID(); return id ? id->toString(QUuid::WithoutBraces) : QString(); }
QString Player::currentAlbumIdString() const {
    const auto id = currentTrackID();
    if (!id) return {};
    for (const Album &a : m_library->albums()) if (a.trackIDs.contains(*id)) return a.id.toString(QUuid::WithoutBraces);
    return {};
}
QString Player::title() const { const Track *t = currentTrack(); return t ? t->title : QString(); }
QString Player::artist() const { const Track *t = currentTrack(); return t ? t->artist : QString(); }
QString Player::albumTitle() const { const Track *t = currentTrack(); return t ? t->albumTitle : QString(); }
double Player::position() const { return m_loaded ? m_engine->position() : m_pausedAt; }
double Player::duration() const {
    if (m_loaded && m_engine->duration() > 0) return m_engine->duration();
    const Track *t = currentTrack();
    return t ? t->duration : 0;
}

void Player::applyVolume() { m_engine->setVolume(m_muted ? 0 : m_volume); }
void Player::setVolume(double v) {
    v = std::clamp(v, 0.0, 1.0);
    m_volume = v;
    if (v > 0) m_muted = false;
    applyVolume();
    emit volumeChanged();
    scheduleSave();
}
void Player::setMuted(bool m) { m_muted = m; applyVolume(); emit volumeChanged(); scheduleSave(); }
void Player::volumeStep(double delta) {
    if (delta > 0) m_muted = false;
    setVolume(m_volume + delta);
}

// ---------- loading ----------

void Player::load(int index, bool autoplay, double at) {
    if (index < 0 || index >= m_queue.size()) return;
    m_index = index;
    m_error.clear();
    emit errorChanged();
    const QString path = pathOf(m_queue[index]);
    if (!autoplay) {
        // Prepare paused without opening the device: playback starts at m_pausedAt.
        m_engine->stop();
        m_loaded = false;
        m_pausedAt = at;
        emit currentChanged();
        emit positionChanged();
        emit stateChanged();
        emit queueChanged();
        scheduleSave();
        return;
    }
    m_wantsPlayback = true;
    m_tokenIndex.clear();
    m_tokenTrack.clear();
    m_loaded = true;
    m_engine->play(path, tokenFor(index), at);
    emit currentChanged();
    emit queueChanged();
    emit stateChanged();
    scheduleSave();
}

qint64 Player::tokenFor(int index) {
    const qint64 token = m_nextToken++;
    m_tokenIndex[token] = index;
    m_tokenTrack[token] = m_queue[index];
    return token;
}

std::optional<int> Player::indexForToken(qint64 token) const {
    if (!m_tokenTrack.contains(token)) return std::nullopt;
    const QUuid track = m_tokenTrack.value(token);
    const int idx = m_tokenIndex.value(token, -1);
    // The queue may have been edited since the token was handed out.
    if (idx >= 0 && idx < m_queue.size() && m_queue[idx] == track) return idx;
    const int found = int(m_queue.indexOf(track));
    return found >= 0 ? std::optional<int>(found) : std::nullopt;
}

// After queue/shuffle/repeat edits: keep the decoded-ahead stream only if it still
// matches what would play next; otherwise restart the current track where it is.
void Player::refreshUpcoming() {
    if (!m_loaded || !m_index) return;
    int prev = *m_index;
    for (qint64 token : m_engine->upcomingTokens()) {
        const auto expected = successor(prev, m_queue.size(), m_repeat, true);
        if (!expected || m_tokenTrack.value(token) != m_queue[*expected]) { load(*m_index, isPlaying(), position()); return; }
        prev = *expected;
    }
    if (m_engine->streamEnded() && successor(prev, m_queue.size(), m_repeat, true))
        load(*m_index, isPlaying(), position());
}

void Player::onEngineTrackChanged(qint64 token) {
    const auto idx = indexForToken(token);
    if (!idx) return;
    m_index = *idx;
    m_consecutiveFailures = 0;
    // Forget tokens for tracks that can no longer be heard.
    for (auto t = m_tokenIndex.begin(); t != m_tokenIndex.end();) t = t.key() < token ? m_tokenIndex.erase(t) : std::next(t);
    for (auto t = m_tokenTrack.begin(); t != m_tokenTrack.end();) t = t.key() < token ? m_tokenTrack.erase(t) : std::next(t);
    if (const auto id = currentTrackID()) {
        m_library->markPlayed(*id);
        emit trackStarted(*id);
    }
    emit currentChanged();
    emit queueChanged();
    scheduleSave();
}

void Player::onEngineFinished() {
    // Queue ran out ("End of queue"): stay on the last track, rewound and paused.
    m_loaded = false;
    m_wantsPlayback = false;
    m_pausedAt = 0;
    emit stateChanged();
    emit positionChanged();
    scheduleSave();
}

void Player::onEngineError(const QString &message) {
    m_error = QString("Couldn’t play “%1”. %2").arg(title(), message);
    emit errorChanged();
    ++m_consecutiveFailures;
    if (!m_wantsPlayback || !m_index) return;
    const auto next = successor(*m_index, m_queue.size(), m_repeat, false);
    if (next && m_consecutiveFailures < std::min<int>(m_queue.size(), 6)) {
        const int idx = *next;
        QTimer::singleShot(1500, this, [this, idx] { if (m_wantsPlayback) load(idx, true); });
    }
}

// ---------- transport ----------

void Player::play() {
    if (!m_index) return;
    if (!m_loaded) {
        const double at = (m_pausedAt >= duration() - 0.25) ? 0 : m_pausedAt;
        load(*m_index, true, at);
        return;
    }
    m_wantsPlayback = true;
    m_engine->resume();
}

void Player::pause() {
    m_wantsPlayback = false;
    if (m_loaded) m_engine->pause();
    scheduleSave();
}

void Player::togglePlayPause() { isPlaying() ? pause() : play(); }

void Player::next() {
    if (!m_index) return;
    if (const auto n = successor(*m_index, m_queue.size(), m_repeat, false)) load(*n, m_wantsPlayback || isPlaying());
}

void Player::previous() {
    if (!m_index) return;
    if (position() > 3) { seek(0); return; }
    if (*m_index > 0) load(*m_index - 1, m_wantsPlayback || isPlaying());
    else if (m_repeat == RepeatMode::All && !m_queue.isEmpty()) load(m_queue.size() - 1, m_wantsPlayback || isPlaying());
    else seek(0);
}

void Player::seek(double seconds) {
    seconds = std::clamp(seconds, 0.0, std::max(0.0, duration()));
    if (m_loaded) m_engine->seek(seconds);
    else { m_pausedAt = seconds; emit positionChanged(); }
    scheduleSave();
}

void Player::stop() {
    m_engine->stop();
    m_loaded = false;
    m_wantsPlayback = false;
    m_pausedAt = 0;
    emit stateChanged();
    emit positionChanged();
}

// ---------- queue edits ----------

void Player::playAlbum(const QUuid &albumID, std::optional<int> startAt, std::optional<bool> shuffled) {
    const Album *album = m_library->album(albumID);
    if (!album || album->trackIDs.isEmpty()) return;
    if (shuffled) { if (*shuffled != shuffle()) m_unshuffled = *shuffled ? std::optional<QVector<QUuid>>(QVector<QUuid>{}) : std::nullopt; emit modesChanged(); }
    int start = startAt.value_or(0);
    if (shuffled && *shuffled && !startAt) start = QRandomGenerator::global()->bounded(int(album->trackIDs.size()));
    setQueue(album->trackIDs, start, true);
}

void Player::setQueue(const QVector<QUuid> &tracks, int startIndex, bool autoplay) {
    if (tracks.isEmpty()) return;
    startIndex = std::clamp(startIndex, 0, int(tracks.size()) - 1);
    if (shuffle()) {
        m_unshuffled = tracks;
        QVector<QUuid> rest = tracks;
        const QUuid first = rest.takeAt(startIndex);
        std::shuffle(rest.begin(), rest.end(), *QRandomGenerator::global());
        rest.prepend(first);
        m_queue = rest;
        startIndex = 0;
    } else {
        m_queue = tracks;
    }
    emit queueChanged();
    load(startIndex, autoplay);
}

void Player::playNext(const QVector<QUuid> &tracks) {
    if (tracks.isEmpty()) return;
    if (!m_index) { setQueue(tracks, 0, false); return; }
    m_unshuffled.reset();
    for (int i = 0; i < tracks.size(); ++i) m_queue.insert(*m_index + 1 + i, tracks[i]);
    emit queueChanged(); emit modesChanged();
    refreshUpcoming();
    scheduleSave();
}

void Player::addToQueue(const QVector<QUuid> &tracks) {
    if (tracks.isEmpty()) return;
    if (!m_index) { setQueue(tracks, 0, false); return; }
    m_unshuffled.reset();
    m_queue += tracks;
    emit queueChanged(); emit modesChanged();
    refreshUpcoming();
    scheduleSave();
}

void Player::jumpTo(int queueIndex) { if (queueIndex >= 0 && queueIndex < m_queue.size()) load(queueIndex, true); }

void Player::removeFromQueue(int i) {
    if (i < 0 || i >= m_queue.size()) return;
    m_unshuffled.reset();
    const bool wasCurrent = m_index && *m_index == i;
    const bool playing = isPlaying();
    m_queue.removeAt(i);
    if (m_queue.isEmpty()) { stop(); m_index.reset(); emit currentChanged(); emit queueChanged(); scheduleSave(); return; }
    if (m_index && i < *m_index) m_index = *m_index - 1;
    if (wasCurrent) load(std::min<int>(i, m_queue.size() - 1), playing);
    else refreshUpcoming();
    emit queueChanged(); emit modesChanged();
    scheduleSave();
}

void Player::moveInQueue(int from, int to) {
    if (from < 0 || from >= m_queue.size() || to < 0 || to >= m_queue.size() || from == to) return;
    m_unshuffled.reset();
    const std::optional<QUuid> current = currentTrackID();
    m_queue.move(from, to);
    if (current) m_index = int(m_queue.indexOf(*current));
    refreshUpcoming();
    emit queueChanged(); emit modesChanged();
    scheduleSave();
}

void Player::clearUpNext() {
    if (!m_index) return;
    m_unshuffled.reset();
    m_queue.resize(*m_index + 1);
    refreshUpcoming();
    emit queueChanged(); emit modesChanged();
    scheduleSave();
}

void Player::setShuffle(bool on) {
    if (on == shuffle()) return;
    if (on) {
        m_unshuffled = m_queue;
        if (m_index) {
            QVector<QUuid> rest = m_queue.mid(*m_index + 1);
            std::shuffle(rest.begin(), rest.end(), *QRandomGenerator::global());
            m_queue = m_queue.mid(0, *m_index + 1) + rest;
        }
    } else {
        const auto current = currentTrackID();
        m_queue = *m_unshuffled;
        m_unshuffled.reset();
        if (current) m_index = std::max<int>(0, m_queue.indexOf(*current));
    }
    refreshUpcoming();
    emit modesChanged(); emit queueChanged();
    scheduleSave();
}

void Player::setRepeatModeInt(int m) {
    m_repeat = RepeatMode(std::clamp(m, 0, 2));
    refreshUpcoming();
    emit modesChanged();
    scheduleSave();
}
void Player::cycleRepeat() { setRepeatModeInt((int(m_repeat) + 1) % 3); }

void Player::purge(const QVector<QUuid> &removed) {
    const QSet<QUuid> gone(removed.begin(), removed.end());
    const auto current = currentTrackID();
    const bool currentGone = current && gone.contains(*current);
    int newIndex = 0;
    QVector<QUuid> kept;
    for (int i = 0; i < m_queue.size(); ++i) {
        if (gone.contains(m_queue[i])) continue;
        if (m_index && i < *m_index) ++newIndex;
        kept.append(m_queue[i]);
    }
    if (m_unshuffled) m_unshuffled->erase(std::remove_if(m_unshuffled->begin(), m_unshuffled->end(), [&](const QUuid &id) { return gone.contains(id); }), m_unshuffled->end());
    m_queue = kept;
    if (m_queue.isEmpty()) { stop(); m_index.reset(); emit currentChanged(); emit queueChanged(); return; }
    if (currentGone) load(std::min<int>(newIndex, m_queue.size() - 1), false);
    else if (current) { m_index = int(m_queue.indexOf(*current)); refreshUpcoming(); }
    emit queueChanged();
    scheduleSave();
}

// ---------- persistence (Playback.json) ----------

void Player::scheduleSave() { m_saveTimer.start(); }

void Player::saveNow() {
    QJsonArray q, u;
    for (const QUuid &id : m_queue) q.append(id.toString(QUuid::WithoutBraces).toUpper());
    QJsonObject o{{"queue", q}, {"position", position()}, {"shuffle", shuffle()},
                  {"repeatMode", m_repeat == RepeatMode::Off ? "off" : m_repeat == RepeatMode::All ? "all" : "one"},
                  {"volume", m_volume}, {"muted", m_muted}};
    if (m_index) o["currentIndex"] = *m_index;
    if (m_unshuffled) { for (const QUuid &id : *m_unshuffled) u.append(id.toString(QUuid::WithoutBraces).toUpper()); o["unshuffled"] = u; }
    QSaveFile f(Paths::dataDir() + "/Playback.json");
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)); f.commit(); }
}

void Player::restore() {
    QFile f(Paths::dataDir() + "/Playback.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QVector<QUuid> saved;
    for (const QJsonValue &v : o["queue"].toArray()) saved.append(QUuid::fromString(v.toString()));
    const int savedIndex = o.contains("currentIndex") ? o["currentIndex"].toInt() : -1;
    const std::optional<QUuid> savedCurrent = (savedIndex >= 0 && savedIndex < saved.size()) ? std::optional(saved[savedIndex]) : std::nullopt;
    for (const QUuid &id : saved) if (m_library->track(id)) m_queue.append(id);
    if (o["unshuffled"].isArray()) {
        QVector<QUuid> u;
        for (const QJsonValue &v : o["unshuffled"].toArray()) { const QUuid id = QUuid::fromString(v.toString()); if (m_library->track(id)) u.append(id); }
        m_unshuffled = u;
    }
    const QString rep = o["repeatMode"].toString();
    m_repeat = rep == "all" ? RepeatMode::All : rep == "one" ? RepeatMode::One : RepeatMode::Off;
    m_volume = std::clamp(o["volume"].toDouble(0.8), 0.0, 1.0);
    m_muted = o["muted"].toBool();
    applyVolume();
    if (savedCurrent && m_queue.contains(*savedCurrent)) {
        m_index = int(m_queue.indexOf(*savedCurrent));
        m_pausedAt = std::max(0.0, o["position"].toDouble());
    } else if (!m_queue.isEmpty()) {
        m_index = 0;
    }
    emit queueChanged(); emit currentChanged(); emit modesChanged(); emit volumeChanged(); emit positionChanged();
}
