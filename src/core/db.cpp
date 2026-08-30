#include "db.hpp"

#include <cstring>
#include <stdexcept>

#include <sqlite3.h>

#include "util.hpp"

namespace scribble {
namespace {

constexpr int kSchemaVersion = 1;

constexpr const char *kSchema = R"sql(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS files (
    id          INTEGER PRIMARY KEY,
    path        TEXT    NOT NULL,
    track       INTEGER NOT NULL DEFAULT 0,
    track_count INTEGER NOT NULL DEFAULT 1,
    size        INTEGER NOT NULL DEFAULT 0,
    mtime       REAL    NOT NULL DEFAULT 0,
    duration    REAL    NOT NULL DEFAULT 0,
    channels    INTEGER NOT NULL DEFAULT 1,
    language    TEXT    NOT NULL DEFAULT '',
    model       TEXT    NOT NULL DEFAULT '',
    status      TEXT    NOT NULL DEFAULT 'pending',
    error       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    UNIQUE(path, track)
);

CREATE TABLE IF NOT EXISTS segments (
    id          INTEGER PRIMARY KEY,
    file_id     INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    idx         INTEGER NOT NULL,
    start       REAL    NOT NULL,
    end         REAL    NOT NULL,
    text        TEXT    NOT NULL,
    local_label TEXT    NOT NULL DEFAULT '',
    global_id   INTEGER,
    avg_logprob REAL    NOT NULL DEFAULT 0,
    no_speech   REAL    NOT NULL DEFAULT 0,
    words       BLOB
);
CREATE INDEX IF NOT EXISTS idx_segments_file ON segments(file_id, idx);

CREATE TABLE IF NOT EXISTS global_speakers (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL DEFAULT '',
    notes      TEXT NOT NULL DEFAULT '',
    centroid   BLOB,
    n_locals   INTEGER NOT NULL DEFAULT 0,
    total_dur  REAL    NOT NULL DEFAULT 0,
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS local_speakers (
    id         INTEGER PRIMARY KEY,
    file_id    INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    label      TEXT    NOT NULL,
    centroid   BLOB,
    n_segments INTEGER NOT NULL DEFAULT 0,
    total_dur  REAL    NOT NULL DEFAULT 0,
    global_id  INTEGER REFERENCES global_speakers(id) ON DELETE SET NULL,
    similarity REAL    NOT NULL DEFAULT 0,
    UNIQUE(file_id, label)
);
CREATE INDEX IF NOT EXISTS idx_local_global ON local_speakers(global_id);

CREATE TABLE IF NOT EXISTS outputs (
    id      INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    format  TEXT    NOT NULL,
    path    TEXT    NOT NULL,
    UNIQUE(file_id, format)
);

-- Pairs a human has looked at and ruled out. Without this every duplicate
-- review starts from scratch and re-presents the same rejected pairs.
CREATE TABLE IF NOT EXISTS dismissed_pairs (
    low        INTEGER NOT NULL,
    high       INTEGER NOT NULL,
    created_at TEXT    NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY(low, high)
);
)sql";

/// Words are stored as a packed blob rather than JSON. There is one row per
/// utterance and several words in each, so this is the hot path for reloading
/// a long transcript, and it avoids pulling in a JSON parser for storage.
std::string pack_words(const std::vector<Word> &words) {
    std::string out;
    auto put_u32 = [&out](std::uint32_t v) {
        char buf[4];
        std::memcpy(buf, &v, 4);
        out.append(buf, 4);
    };
    auto put_f32 = [&out](float v) {
        char buf[4];
        std::memcpy(buf, &v, 4);
        out.append(buf, 4);
    };

    put_u32(static_cast<std::uint32_t>(words.size()));
    for (const auto &w : words) {
        put_f32(static_cast<float>(w.start));
        put_f32(static_cast<float>(w.end));
        put_f32(w.probability);
        put_u32(static_cast<std::uint32_t>(w.text.size()));
        out.append(w.text);
    }
    return out;
}

std::vector<Word> unpack_words(const void *blob, int bytes) {
    std::vector<Word> out;
    if (blob == nullptr || bytes < 4) {
        return out;
    }
    const auto *p = static_cast<const char *>(blob);
    int offset = 0;

    auto get_u32 = [&](std::uint32_t *v) {
        if (offset + 4 > bytes) return false;
        std::memcpy(v, p + offset, 4);
        offset += 4;
        return true;
    };
    auto get_f32 = [&](float *v) {
        if (offset + 4 > bytes) return false;
        std::memcpy(v, p + offset, 4);
        offset += 4;
        return true;
    };

    std::uint32_t count = 0;
    if (!get_u32(&count)) {
        return out;
    }
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Word w;
        float start = 0, end = 0, prob = 0;
        std::uint32_t len = 0;
        if (!get_f32(&start) || !get_f32(&end) || !get_f32(&prob) || !get_u32(&len)) {
            break;
        }
        if (offset + static_cast<int>(len) > bytes) {
            break;
        }
        w.start = start;
        w.end = end;
        w.probability = prob;
        w.text.assign(p + offset, len);
        offset += static_cast<int>(len);
        out.push_back(std::move(w));
    }
    return out;
}

std::vector<float> unpack_vector(const void *blob, int bytes) {
    std::vector<float> out;
    if (blob == nullptr || bytes < 4) {
        return out;
    }
    out.resize(static_cast<size_t>(bytes) / sizeof(float));
    std::memcpy(out.data(), blob, out.size() * sizeof(float));
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

/// RAII around a prepared statement. Every query in this file goes through it,
/// so a thrown exception mid-query cannot leak a statement handle and wedge a
/// later schema change behind SQLITE_LOCKED.
class Stmt {
public:
    Stmt(sqlite3 *db, const char *sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("prepare failed: ") + sqlite3_errmsg(db) +
                                     " for " + sql);
        }
    }
    ~Stmt() { sqlite3_finalize(stmt_); }

    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    Stmt &bind(int i, std::int64_t v) { sqlite3_bind_int64(stmt_, i, v); return *this; }
    Stmt &bind(int i, int v) { sqlite3_bind_int(stmt_, i, v); return *this; }
    Stmt &bind(int i, double v) { sqlite3_bind_double(stmt_, i, v); return *this; }
    Stmt &bind(int i, const std::string &v) {
        sqlite3_bind_text(stmt_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt &bind_blob(int i, const void *data, size_t bytes) {
        if (data == nullptr || bytes == 0) {
            sqlite3_bind_null(stmt_, i);
        } else {
            sqlite3_bind_blob(stmt_, i, data, static_cast<int>(bytes), SQLITE_TRANSIENT);
        }
        return *this;
    }
    Stmt &bind(int i, const std::vector<float> &v) {
        return bind_blob(i, v.data(), v.size() * sizeof(float));
    }
    Stmt &bind_null(int i) { sqlite3_bind_null(stmt_, i); return *this; }
    Stmt &bind_id_or_null(int i, std::int64_t id) {
        if (id < 0) {
            sqlite3_bind_null(stmt_, i);
        } else {
            sqlite3_bind_int64(stmt_, i, id);
        }
        return *this;
    }

    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("step failed: ") + sqlite3_errmsg(db_));
    }

    void run() {
        while (step()) {}
        reset();
    }

    /// Statements are reused in loops, so bindings are cleared with the reset.
    /// Without this an insert loop silently fails on its second row.
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    std::int64_t col_int(int i) const { return sqlite3_column_int64(stmt_, i); }
    double col_double(int i) const { return sqlite3_column_double(stmt_, i); }
    std::string col_text(int i) const {
        const auto *p = sqlite3_column_text(stmt_, i);
        return p ? reinterpret_cast<const char *>(p) : std::string{};
    }
    bool col_is_null(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_NULL; }
    std::vector<float> col_vector(int i) const {
        return unpack_vector(sqlite3_column_blob(stmt_, i), sqlite3_column_bytes(stmt_, i));
    }
    std::vector<Word> col_words(int i) const {
        return unpack_words(sqlite3_column_blob(stmt_, i), sqlite3_column_bytes(stmt_, i));
    }

private:
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
};

// ---------------------------------------------------------------------------

struct Database::Impl {
    sqlite3 *db = nullptr;

    void exec(const char *sql) {
        char *err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string message = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("sqlite exec failed: " + message);
        }
    }
};

Database::Database(const fs::path &path) : impl_(std::make_unique<Impl>()) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }

    int rc = sqlite3_open_v2(path.string().c_str(), &impl_->db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::string message = impl_->db ? sqlite3_errmsg(impl_->db) : "cannot open database";
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        throw std::runtime_error("cannot open " + path.string() + ": " + message);
    }

    sqlite3_busy_timeout(impl_->db, 30000);
    impl_->exec(kSchema);

    Stmt set(impl_->db, "INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version', ?)");
    set.bind(1, std::to_string(kSchemaVersion));
    set.run();
}

Database::~Database() {
    if (impl_ && impl_->db) {
        sqlite3_close(impl_->db);
    }
}

sqlite3 *Database::handle() const noexcept { return impl_->db; }

void Database::begin() { impl_->exec("BEGIN IMMEDIATE"); }
void Database::commit() { impl_->exec("COMMIT"); }
void Database::rollback() {
    char *err = nullptr;
    sqlite3_exec(impl_->db, "ROLLBACK", nullptr, nullptr, &err);
    sqlite3_free(err);
}

// -- files ------------------------------------------------------------------

std::int64_t Database::upsert_file(const FileRecord &rec, bool *changed) {
    if (changed) {
        *changed = true;
    }

    Stmt find(impl_->db,
              "SELECT id, size, mtime, status FROM files WHERE path = ? AND track = ?");
    find.bind(1, rec.path).bind(2, rec.track);
    if (find.step()) {
        std::int64_t id = find.col_int(0);
        std::int64_t size = find.col_int(1);
        double mtime = find.col_double(2);
        std::string status = find.col_text(3);

        // Size and mtime are what invalidate a previous transcription. An
        // untouched file that already completed is left alone.
        bool same = size == rec.size && std::abs(mtime - rec.mtime) < 1.0;
        if (changed) {
            *changed = !(same && status == "done");
        }
        if (!same) {
            Stmt upd(impl_->db,
                     "UPDATE files SET size = ?, mtime = ?, status = 'pending', error = '', "
                     "updated_at = datetime('now') WHERE id = ?");
            upd.bind(1, rec.size).bind(2, rec.mtime).bind(3, id);
            upd.run();
        }
        return id;
    }

    Stmt ins(impl_->db,
             "INSERT INTO files(path, track, track_count, size, mtime, duration, channels, "
             "language, model, status) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending')");
    ins.bind(1, rec.path)
        .bind(2, rec.track)
        .bind(3, rec.track_count)
        .bind(4, rec.size)
        .bind(5, rec.mtime)
        .bind(6, rec.duration)
        .bind(7, rec.channels)
        .bind(8, rec.language)
        .bind(9, rec.model);
    ins.run();
    return sqlite3_last_insert_rowid(impl_->db);
}

namespace {

FileRecord read_file_row(const Stmt &s) {
    FileRecord r;
    r.id = s.col_int(0);
    r.path = s.col_text(1);
    r.track = static_cast<int>(s.col_int(2));
    r.track_count = static_cast<int>(s.col_int(3));
    r.size = s.col_int(4);
    r.mtime = s.col_double(5);
    r.duration = s.col_double(6);
    r.channels = static_cast<int>(s.col_int(7));
    r.language = s.col_text(8);
    r.model = s.col_text(9);
    r.status = s.col_text(10);
    r.error = s.col_text(11);
    r.updated_at = s.col_text(12);
    return r;
}

constexpr const char *kFileColumns =
    "id, path, track, track_count, size, mtime, duration, channels, language, model, "
    "status, error, updated_at";

}  // namespace

std::optional<FileRecord> Database::file(std::int64_t id) const {
    Stmt s(impl_->db, ("SELECT " + std::string(kFileColumns) + " FROM files WHERE id = ?").c_str());
    s.bind(1, id);
    if (s.step()) {
        return read_file_row(s);
    }
    return std::nullopt;
}

std::vector<FileRecord> Database::files(const std::string &status_filter) const {
    std::string sql = "SELECT " + std::string(kFileColumns) + " FROM files";
    if (!status_filter.empty()) {
        sql += " WHERE status = ?";
    }
    sql += " ORDER BY path, track";

    Stmt s(impl_->db, sql.c_str());
    if (!status_filter.empty()) {
        s.bind(1, status_filter);
    }
    std::vector<FileRecord> out;
    while (s.step()) {
        out.push_back(read_file_row(s));
    }
    return out;
}

void Database::set_file_status(std::int64_t id, const std::string &status,
                               const std::string &error) {
    Stmt s(impl_->db,
           "UPDATE files SET status = ?, error = ?, updated_at = datetime('now') WHERE id = ?");
    s.bind(1, status).bind(2, error).bind(3, id);
    s.run();
}

void Database::update_file_meta(std::int64_t id, double duration, int channels,
                                const std::string &language, const std::string &model) {
    Stmt s(impl_->db,
           "UPDATE files SET duration = ?, channels = ?, language = ?, model = ?, "
           "updated_at = datetime('now') WHERE id = ?");
    s.bind(1, duration).bind(2, channels).bind(3, language).bind(4, model).bind(5, id);
    s.run();
}

// -- segments ---------------------------------------------------------------

void Database::clear_segments(std::int64_t file_id) {
    Stmt s(impl_->db, "DELETE FROM segments WHERE file_id = ?");
    s.bind(1, file_id);
    s.run();
}

void Database::insert_segments(std::int64_t file_id, const std::vector<Segment> &segments) {
    Stmt s(impl_->db,
           "INSERT INTO segments(file_id, idx, start, end, text, local_label, global_id, "
           "avg_logprob, no_speech, words) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const auto &seg : segments) {
        std::string packed = pack_words(seg.words);
        s.bind(1, file_id)
            .bind(2, seg.index)
            .bind(3, seg.start)
            .bind(4, seg.end)
            .bind(5, seg.text)
            .bind(6, seg.local_label)
            .bind_id_or_null(7, seg.global_id)
            .bind(8, static_cast<double>(seg.avg_logprob))
            .bind(9, static_cast<double>(seg.no_speech))
            .bind_blob(10, packed.data(), packed.size());
        s.run();
    }
}

std::vector<Segment> Database::segments(std::int64_t file_id) const {
    Stmt s(impl_->db,
           "SELECT idx, start, end, text, local_label, global_id, avg_logprob, no_speech, "
           "words FROM segments WHERE file_id = ? ORDER BY idx");
    s.bind(1, file_id);

    std::vector<Segment> out;
    while (s.step()) {
        Segment seg;
        seg.index = static_cast<int>(s.col_int(0));
        seg.start = s.col_double(1);
        seg.end = s.col_double(2);
        seg.text = s.col_text(3);
        seg.local_label = s.col_text(4);
        seg.global_id = s.col_is_null(5) ? -1 : s.col_int(5);
        seg.avg_logprob = static_cast<float>(s.col_double(6));
        seg.no_speech = static_cast<float>(s.col_double(7));
        seg.words = s.col_words(8);
        out.push_back(std::move(seg));
    }
    return out;
}

// -- speakers ---------------------------------------------------------------

void Database::clear_local_speakers(std::int64_t file_id) {
    Stmt s(impl_->db, "DELETE FROM local_speakers WHERE file_id = ?");
    s.bind(1, file_id);
    s.run();
}

std::int64_t Database::insert_local_speaker(const LocalSpeaker &sp) {
    Stmt s(impl_->db,
           "INSERT INTO local_speakers(file_id, label, centroid, n_segments, total_dur, "
           "global_id, similarity) VALUES(?, ?, ?, ?, ?, ?, ?) "
           "ON CONFLICT(file_id, label) DO UPDATE SET centroid = excluded.centroid, "
           "n_segments = excluded.n_segments, total_dur = excluded.total_dur");
    s.bind(1, sp.file_id)
        .bind(2, sp.label)
        .bind(3, sp.centroid)
        .bind(4, sp.n_segments)
        .bind(5, sp.total_duration)
        .bind_id_or_null(6, sp.global_id)
        .bind(7, static_cast<double>(sp.similarity));
    s.run();

    Stmt find(impl_->db, "SELECT id FROM local_speakers WHERE file_id = ? AND label = ?");
    find.bind(1, sp.file_id).bind(2, sp.label);
    return find.step() ? find.col_int(0) : -1;
}

namespace {

LocalSpeaker read_local_row(const Stmt &s) {
    LocalSpeaker sp;
    sp.id = s.col_int(0);
    sp.file_id = s.col_int(1);
    sp.label = s.col_text(2);
    sp.centroid = s.col_vector(3);
    sp.n_segments = static_cast<int>(s.col_int(4));
    sp.total_duration = s.col_double(5);
    sp.global_id = s.col_is_null(6) ? -1 : s.col_int(6);
    sp.similarity = static_cast<float>(s.col_double(7));
    return sp;
}

constexpr const char *kLocalColumns =
    "id, file_id, label, centroid, n_segments, total_dur, global_id, similarity";

}  // namespace

std::vector<LocalSpeaker> Database::local_speakers(std::int64_t file_id) const {
    Stmt s(impl_->db, ("SELECT " + std::string(kLocalColumns) +
                       " FROM local_speakers WHERE file_id = ? ORDER BY label")
                          .c_str());
    s.bind(1, file_id);
    std::vector<LocalSpeaker> out;
    while (s.step()) {
        out.push_back(read_local_row(s));
    }
    return out;
}

std::vector<LocalSpeaker> Database::all_local_speakers() const {
    Stmt s(impl_->db, ("SELECT " + std::string(kLocalColumns) +
                       " FROM local_speakers ORDER BY id")
                          .c_str());
    std::vector<LocalSpeaker> out;
    while (s.step()) {
        out.push_back(read_local_row(s));
    }
    return out;
}

void Database::assign_local(std::int64_t local_id, std::int64_t global_id, float similarity) {
    Stmt s(impl_->db,
           "UPDATE local_speakers SET global_id = ?, similarity = ? WHERE id = ?");
    s.bind_id_or_null(1, global_id).bind(2, static_cast<double>(similarity)).bind(3, local_id);
    s.run();

    // Segments carry the identity too so a transcript can be rendered without
    // joining back through the speaker tables on every line.
    Stmt seg(impl_->db,
             "UPDATE segments SET global_id = ? WHERE file_id = "
             "(SELECT file_id FROM local_speakers WHERE id = ?) AND local_label = "
             "(SELECT label FROM local_speakers WHERE id = ?)");
    seg.bind_id_or_null(1, global_id).bind(2, local_id).bind(3, local_id);
    seg.run();
}

std::int64_t Database::create_global(const std::vector<float> &centroid, double duration) {
    Stmt s(impl_->db,
           "INSERT INTO global_speakers(name, centroid, n_locals, total_dur) "
           "VALUES('', ?, 1, ?)");
    s.bind(1, centroid).bind(2, duration);
    s.run();
    return sqlite3_last_insert_rowid(impl_->db);
}

void Database::update_global_centroid(std::int64_t global_id,
                                      const std::vector<float> &centroid, int n_locals,
                                      double duration) {
    Stmt s(impl_->db,
           "UPDATE global_speakers SET centroid = ?, n_locals = ?, total_dur = ? WHERE id = ?");
    s.bind(1, centroid).bind(2, n_locals).bind(3, duration).bind(4, global_id);
    s.run();
}

std::vector<GlobalSpeaker> Database::globals() const {
    Stmt s(impl_->db,
           "SELECT g.id, g.name, g.notes, g.n_locals, g.total_dur, g.created_at, "
           "(SELECT COUNT(DISTINCT file_id) FROM local_speakers WHERE global_id = g.id) "
           "FROM global_speakers g ORDER BY g.total_dur DESC, g.id");
    std::vector<GlobalSpeaker> out;
    while (s.step()) {
        GlobalSpeaker g;
        g.id = s.col_int(0);
        g.name = s.col_text(1);
        g.notes = s.col_text(2);
        g.n_locals = static_cast<int>(s.col_int(3));
        g.total_duration = s.col_double(4);
        g.created_at = s.col_text(5);
        g.n_files = static_cast<int>(s.col_int(6));
        out.push_back(std::move(g));
    }
    return out;
}

std::optional<GlobalSpeaker> Database::global(std::int64_t id) const {
    Stmt s(impl_->db,
           "SELECT g.id, g.name, g.notes, g.n_locals, g.total_dur, g.created_at, "
           "(SELECT COUNT(DISTINCT file_id) FROM local_speakers WHERE global_id = g.id) "
           "FROM global_speakers g WHERE g.id = ?");
    s.bind(1, id);
    if (!s.step()) {
        return std::nullopt;
    }
    GlobalSpeaker g;
    g.id = s.col_int(0);
    g.name = s.col_text(1);
    g.notes = s.col_text(2);
    g.n_locals = static_cast<int>(s.col_int(3));
    g.total_duration = s.col_double(4);
    g.created_at = s.col_text(5);
    g.n_files = static_cast<int>(s.col_int(6));
    return g;
}

std::vector<float> Database::global_centroid(std::int64_t id) const {
    Stmt s(impl_->db, "SELECT centroid FROM global_speakers WHERE id = ?");
    s.bind(1, id);
    return s.step() ? s.col_vector(0) : std::vector<float>{};
}

void Database::rename_global(std::int64_t id, const std::string &name) {
    Stmt s(impl_->db, "UPDATE global_speakers SET name = ? WHERE id = ?");
    s.bind(1, name).bind(2, id);
    s.run();
}

void Database::set_global_notes(std::int64_t id, const std::string &notes) {
    Stmt s(impl_->db, "UPDATE global_speakers SET notes = ? WHERE id = ?");
    s.bind(1, notes).bind(2, id);
    s.run();
}

int Database::merge_globals(std::int64_t from, std::int64_t into) {
    if (from == into) {
        return 0;
    }

    Stmt move(impl_->db, "UPDATE local_speakers SET global_id = ? WHERE global_id = ?");
    move.bind(1, into).bind(2, from);
    move.run();
    int moved = sqlite3_changes(impl_->db);

    Stmt seg(impl_->db, "UPDATE segments SET global_id = ? WHERE global_id = ?");
    seg.bind(1, into).bind(2, from);
    seg.run();

    // The surviving identity keeps whichever name exists, so merging an
    // unnamed duplicate into a named one never discards the human's work.
    Stmt name(impl_->db,
              "UPDATE global_speakers SET name = "
              "CASE WHEN name != '' THEN name ELSE (SELECT name FROM global_speakers WHERE id = ?) "
              "END WHERE id = ?");
    name.bind(1, from).bind(2, into);
    name.run();

    Stmt stats(impl_->db,
               "UPDATE global_speakers SET "
               "n_locals = (SELECT COUNT(*) FROM local_speakers WHERE global_id = ?), "
               "total_dur = (SELECT COALESCE(SUM(total_dur), 0) FROM local_speakers "
               "WHERE global_id = ?) WHERE id = ?");
    stats.bind(1, into).bind(2, into).bind(3, into);
    stats.run();

    Stmt del(impl_->db, "DELETE FROM global_speakers WHERE id = ?");
    del.bind(1, from);
    del.run();

    // Dismissals naming the identity that just disappeared are meaningless and
    // would otherwise suppress a genuine future candidate at the same id.
    Stmt stale(impl_->db, "DELETE FROM dismissed_pairs WHERE low = ? OR high = ?");
    stale.bind(1, from).bind(2, from);
    stale.run();

    return moved;
}

std::int64_t Database::split_local(std::int64_t local_id) {
    Stmt s(impl_->db, "SELECT centroid, total_dur FROM local_speakers WHERE id = ?");
    s.bind(1, local_id);
    if (!s.step()) {
        return -1;
    }
    std::vector<float> centroid = s.col_vector(0);
    double duration = s.col_double(1);

    std::int64_t fresh = create_global(centroid, duration);
    assign_local(local_id, fresh, 1.0f);
    delete_empty_globals();
    return fresh;
}

void Database::delete_empty_globals() {
    // A named identity is kept even with nothing attached: the name is human
    // work and the voice may well reappear in the next batch.
    impl_->exec(
        "DELETE FROM global_speakers WHERE name = '' AND id NOT IN "
        "(SELECT DISTINCT global_id FROM local_speakers WHERE global_id IS NOT NULL)");
}

std::vector<std::int64_t> Database::globals_in_file(std::int64_t file_id) const {
    Stmt s(impl_->db,
           "SELECT DISTINCT global_id FROM local_speakers "
           "WHERE file_id = ? AND global_id IS NOT NULL");
    s.bind(1, file_id);
    std::vector<std::int64_t> out;
    while (s.step()) {
        out.push_back(s.col_int(0));
    }
    return out;
}

// -- duplicate review -------------------------------------------------------

namespace {

/// Ordered so a pair has one representation regardless of which identity the
/// caller happens to name first.
std::pair<std::int64_t, std::int64_t> canonical_pair(std::int64_t a, std::int64_t b) {
    return a <= b ? std::make_pair(a, b) : std::make_pair(b, a);
}

}  // namespace

void Database::dismiss_pair(std::int64_t a, std::int64_t b) {
    auto [low, high] = canonical_pair(a, b);
    Stmt s(impl_->db,
           "INSERT OR IGNORE INTO dismissed_pairs(low, high) VALUES(?, ?)");
    s.bind(1, low).bind(2, high);
    s.run();
}

bool Database::pair_dismissed(std::int64_t a, std::int64_t b) const {
    auto [low, high] = canonical_pair(a, b);
    Stmt s(impl_->db, "SELECT 1 FROM dismissed_pairs WHERE low = ? AND high = ?");
    s.bind(1, low).bind(2, high);
    return s.step();
}

void Database::undismiss_pair(std::int64_t a, std::int64_t b) {
    auto [low, high] = canonical_pair(a, b);
    Stmt s(impl_->db, "DELETE FROM dismissed_pairs WHERE low = ? AND high = ?");
    s.bind(1, low).bind(2, high);
    s.run();
}

std::vector<std::pair<std::int64_t, std::int64_t>> Database::dismissed_pairs() const {
    Stmt s(impl_->db, "SELECT low, high FROM dismissed_pairs ORDER BY low, high");
    std::vector<std::pair<std::int64_t, std::int64_t>> out;
    while (s.step()) {
        out.emplace_back(s.col_int(0), s.col_int(1));
    }
    return out;
}

// -- destructive ------------------------------------------------------------

void Database::clear_all() {
    // Ordered so foreign keys never block a delete, then vacuumed because the
    // usual reason to clear is that the file filled up with a bad batch.
    impl_->exec(
        "BEGIN IMMEDIATE;"
        "DELETE FROM outputs;"
        "DELETE FROM segments;"
        "DELETE FROM local_speakers;"
        "DELETE FROM global_speakers;"
        "DELETE FROM dismissed_pairs;"
        "DELETE FROM files;"
        "COMMIT;");
    impl_->exec("VACUUM");
}

bool Database::backup_to(const fs::path &dest, std::string *error) {
    std::error_code ec;
    if (dest.has_parent_path()) {
        fs::create_directories(dest.parent_path(), ec);
    }

    sqlite3 *out = nullptr;
    if (sqlite3_open_v2(dest.string().c_str(), &out,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (error) {
            *error = out ? sqlite3_errmsg(out) : "cannot open backup destination";
        }
        sqlite3_close(out);
        return false;
    }

    sqlite3_backup *backup = sqlite3_backup_init(out, "main", impl_->db, "main");
    if (backup == nullptr) {
        if (error) {
            *error = sqlite3_errmsg(out);
        }
        sqlite3_close(out);
        return false;
    }

    sqlite3_backup_step(backup, -1);
    const int rc = sqlite3_backup_finish(backup);
    if (rc != SQLITE_OK && error) {
        *error = sqlite3_errmsg(out);
    }
    sqlite3_close(out);

    if (rc != SQLITE_OK) {
        fs::remove(dest, ec);
        return false;
    }
    return true;
}

Database::Counts Database::counts() const {
    Counts c;
    Stmt s(impl_->db,
           "SELECT (SELECT COUNT(*) FROM files),"
           "       (SELECT COUNT(*) FROM segments),"
           "       (SELECT COUNT(*) FROM global_speakers),"
           "       (SELECT COUNT(*) FROM global_speakers WHERE name != '')");
    if (s.step()) {
        c.files = static_cast<int>(s.col_int(0));
        c.segments = static_cast<int>(s.col_int(1));
        c.speakers = static_cast<int>(s.col_int(2));
        c.named_speakers = static_cast<int>(s.col_int(3));
    }
    return c;
}

// -- outputs ----------------------------------------------------------------
void Database::record_output(std::int64_t file_id, const std::string &format,
                             const std::string &path) {
    Stmt s(impl_->db,
           "INSERT INTO outputs(file_id, format, path) VALUES(?, ?, ?) "
           "ON CONFLICT(file_id, format) DO UPDATE SET path = excluded.path");
    s.bind(1, file_id).bind(2, format).bind(3, path);
    s.run();
}

std::vector<std::string> Database::outputs(std::int64_t file_id) const {
    Stmt s(impl_->db, "SELECT path FROM outputs WHERE file_id = ? ORDER BY format");
    s.bind(1, file_id);
    std::vector<std::string> out;
    while (s.step()) {
        out.push_back(s.col_text(0));
    }
    return out;
}

}  // namespace scribble
