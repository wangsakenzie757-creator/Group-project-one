"""
Project OASIS – test version of the database using SQLite.

SQLite is built into Python: no server, no install, no login.
The whole database is saved in one file: oasis.db (next to this script).

Tables from the ERD (our own sensors):
    User, Area, Sensor, Reading
Extra tables for the open datasets (used to train and test the AI model):
    Station            – every outside measuring station (PM stations and KNMI 370)
    AirQualityHistory  – hourly PM2.5 and PM10 from the yearly *_PM25/_PM10_English.xlsx files
    WeatherHistory     – hourly temperature, humidity and pressure from the KNMI file
    ImportLog          – which files are already imported, so a re-run skips them

The open data is kept apart from Reading on purpose: Reading only holds
values from our own nodes, so measured, official and predicted values are never mixed.

Needs:  pip install pandas openpyxl
Run:    python create_oasis_sqlite.py
"""

import sqlite3
from pathlib import Path

import pandas as pd

BASE_DIR = Path(__file__).parent
DB_FILE = BASE_DIR / "oasis.db"

# The datasets are searched for in this folder and ALL its subfolders.
# BASE_DIR.parent = the folder above this script (e.g. OASIS when the script is in OASIS\Kenzie).
# To point somewhere else, use for example: DATA_ROOT = Path(r"C:\Users\you\Desktop\OASIS")
DATA_ROOT = BASE_DIR.parent
PM_PATTERNS = {"pm_2_5": "*_PM25_English.xlsx", "pm_10": "*_PM10_English.xlsx"}
KNMI_PATTERN = "KNMI_Eindhoven_hourly_*.xlsx"
KNMI_STATION_ID = "KNMI-370"

# Only import these PM stations (column names in the xlsx). None = all stations.
PM_STATION_FILTER = None   # e.g. ["NL10236", "NL10237"]

BATCH_SIZE = 50_000


TABLES = {
    "Area": """
        CREATE TABLE IF NOT EXISTS Area (
            area_id  INTEGER PRIMARY KEY AUTOINCREMENT,
            name     TEXT NOT NULL,
            city     TEXT NOT NULL DEFAULT 'Eindhoven'
        )
    """,
    "User": """
        CREATE TABLE IF NOT EXISTS User (
            user_id        INTEGER PRIMARY KEY AUTOINCREMENT,
            password_hash  TEXT NOT NULL,
            email          TEXT NOT NULL UNIQUE,
            username       TEXT NOT NULL UNIQUE,
            role           TEXT NOT NULL DEFAULT 'viewer'
                           CHECK (role IN ('admin', 'viewer')),
            last_login     TEXT,
            created_at     TEXT NOT NULL DEFAULT (datetime('now'))
        )
    """,
    "Sensor": """
        CREATE TABLE IF NOT EXISTS Sensor (
            sensor_id     INTEGER PRIMARY KEY AUTOINCREMENT,
            area_id       INTEGER NOT NULL
                          REFERENCES Area (area_id)
                          ON UPDATE CASCADE ON DELETE RESTRICT,
            user_id       INTEGER NOT NULL
                          REFERENCES User (user_id)
                          ON UPDATE CASCADE ON DELETE RESTRICT,
            hardware      TEXT NOT NULL,
            installed_at  TEXT NOT NULL DEFAULT (datetime('now'))
        )
    """,
    "Reading": """
        CREATE TABLE IF NOT EXISTS Reading (
            reading_id   INTEGER PRIMARY KEY AUTOINCREMENT,
            sensor_id    INTEGER NOT NULL
                         REFERENCES Sensor (sensor_id)
                         ON UPDATE CASCADE ON DELETE CASCADE,
            pm_1         REAL,    -- µg/m³ (SPS30)
            pm_2_5       REAL,    -- µg/m³ (SPS30)
            pm_10        REAL,    -- µg/m³ (SPS30)
            temperature  REAL,    -- °C (BME280)
            humidity     REAL,    -- % RH (BME280)
            pressure     REAL,    -- hPa (BME280)
            measured_at  TEXT NOT NULL,   -- UTC, 'YYYY-MM-DD HH:MM:SS'
            UNIQUE (sensor_id, measured_at)
        )
    """,
    "Station": """
        CREATE TABLE IF NOT EXISTS Station (
            station_id  TEXT PRIMARY KEY,          -- e.g. 'NL10236' or 'KNMI-370'
            source      TEXT NOT NULL,             -- 'PM dataset' or 'KNMI'
            name        TEXT
        )
    """,
    "AirQualityHistory": """
        CREATE TABLE IF NOT EXISTS AirQualityHistory (
            station_id   TEXT NOT NULL REFERENCES Station (station_id),
            measured_at  TEXT NOT NULL,   -- 'YYYY-MM-DD HH:MM:SS', as in the source file
            pm_2_5       REAL,            -- µg/m³
            pm_10        REAL,            -- µg/m³
            PRIMARY KEY (station_id, measured_at)
        )
    """,
    "WeatherHistory": """
        CREATE TABLE IF NOT EXISTS WeatherHistory (
            station_id   TEXT NOT NULL REFERENCES Station (station_id),
            measured_at  TEXT NOT NULL,   -- UTC, end of the KNMI hour
            temperature  REAL,            -- °C
            humidity     REAL,            -- %
            pressure     REAL,            -- hPa, reduced to sea level
            PRIMARY KEY (station_id, measured_at)
        )
    """,
    "ImportLog": """
        CREATE TABLE IF NOT EXISTS ImportLog (
            file_name    TEXT PRIMARY KEY,
            rows_loaded  INTEGER NOT NULL,
            imported_at  TEXT NOT NULL DEFAULT (datetime('now'))
        )
    """,
}

INDEXES = [
    "CREATE INDEX IF NOT EXISTS idx_aq_time ON AirQualityHistory (measured_at)",
    "CREATE INDEX IF NOT EXISTS idx_weather_time ON WeatherHistory (measured_at)",
]


def create_tables(cur):
    for name, ddl in TABLES.items():
        cur.execute(ddl)
        print(f"Table '{name}' is ready.")
    for ddl in INDEXES:
        cur.execute(ddl)


def insert_example_data(cur):
    """One area, one admin user, one sensor and three readings."""
    cur.execute("SELECT COUNT(*) FROM Area")
    if cur.fetchone()[0] > 0:
        print("Example data already exists, skipped.")
        return

    cur.execute("INSERT INTO Area (name, city) VALUES (?, ?)", ("Strijp-S", "Eindhoven"))
    area_id = cur.lastrowid

    cur.execute(
        "INSERT INTO User (password_hash, email, username, role) VALUES (?, ?, ?, ?)",
        ("replace_with_a_real_hash", "admin@example.com", "admin", "admin"),
    )
    user_id = cur.lastrowid

    cur.execute(
        "INSERT INTO Sensor (area_id, user_id, hardware) VALUES (?, ?, ?)",
        (area_id, user_id, "ESP32 + SPS30 + BME280"),
    )
    sensor_id = cur.lastrowid

    readings = [
        (sensor_id, 4.2, 6.8, 9.1, 11.5, 82.0, 1013.2, "2026-11-17 09:00:00"),
        (sensor_id, 4.5, 7.1, 9.6, 11.8, 81.0, 1013.0, "2026-11-17 09:10:00"),
        (sensor_id, 5.0, 7.9, 10.4, 12.1, 80.0, 1012.8, "2026-11-17 09:20:00"),
    ]
    cur.executemany(
        "INSERT INTO Reading (sensor_id, pm_1, pm_2_5, pm_10, "
        "temperature, humidity, pressure, measured_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        readings,
    )
    print("Example data inserted.")


# ---------- Reading the dataset files ----------

def find_files(pattern):
    """Find every file matching the pattern in DATA_ROOT and its subfolders (one per file name)."""
    found = {}
    for path in sorted(DATA_ROOT.rglob(pattern)):
        if path.name.startswith("~$"):        # Excel lock files of opened workbooks
            continue
        found.setdefault(path.name, path)     # same file in two places: use the first
    print(f"  Searched {DATA_ROOT} for '{pattern}': {len(found)} file(s)")
    for path in found.values():
        print(f"    {path}")
    return list(found.values())


def is_text(series):
    return not pd.api.types.is_numeric_dtype(series) and not pd.api.types.is_datetime64_any_dtype(series)


def to_number(series):
    """Turn text like '12,4', '-', 'n/a' into numbers (or empty)."""
    if is_text(series):
        series = series.astype(str).str.strip().str.replace(",", ".", regex=False)
    values = pd.to_numeric(series, errors="coerce")
    return values.where(values >= 0)   # negative values are error codes, not pollution


def to_datetime(series):
    if pd.api.types.is_datetime64_any_dtype(series):
        return series
    if pd.api.types.is_numeric_dtype(series):
        return pd.Series(pd.NaT, index=series.index)   # plain numbers are not dates
    text = series.astype(str).str.strip()
    looks_like_date = text.str.contains(r"\d{1,4}[-/.]\d{1,2}[-/.]\d{2,4}", regex=True)
    parsed = pd.to_datetime(text.where(looks_like_date), errors="coerce",
                            dayfirst=True, format="mixed")
    return parsed


def find_header_row(path):
    """The header is the row right above the first rows that contain dates."""
    raw = pd.read_excel(path, header=None, nrows=60)
    for r in range(min(20, len(raw) - 1)):
        below = raw.iloc[r + 1:r + 6]          # the next 5 rows must all be dates
        for col in raw.columns:
            sample = below[col]
            if len(sample) == 5 and to_datetime(sample).notna().all():
                header_cell = raw.iat[r, col]
                if pd.notna(header_cell) and to_datetime(pd.Series([header_cell])).isna().all():
                    return r
    raise ValueError(f"No date/time column found in {path.name}")


def read_pm_file(path):
    """
    Read one yearly PM file and return a long table:
        station_id | measured_at | value

    The file has a time column and either
      - one column per station (wide), or
      - a station column and a value column (long).
    Title rows above the header are skipped automatically.
    """
    df = pd.read_excel(path, header=find_header_row(path))
    df.columns = [str(c).strip() for c in df.columns]
    df = df.loc[:, [not c.startswith("Unnamed") or df[c].notna().any() for c in df.columns]]

    date_cols = [c for c in df.columns
                 if to_datetime(df[c].dropna().head(50)).notna().mean() > 0.9]
    # With a begin and an end column, use the end of the hour (same as KNMI)
    time_col = date_cols[-1]
    df["measured_at"] = to_datetime(df[time_col])
    df = df.dropna(subset=["measured_at"])
    other = [c for c in df.columns if c not in date_cols + ["measured_at"]]

    text_cols = [c for c in other if is_text(df[c]) and to_number(df[c]).notna().mean() < 0.5]
    num_cols = [c for c in other if c not in text_cols and to_number(df[c]).notna().any()]

    station_like = [c for c in text_cols if "station" in c.lower() or "location" in c.lower()]
    if station_like and len(num_cols) >= 1:
        # Long layout: station column + value column
        long = pd.DataFrame({
            "station_id": df[station_like[0]].astype(str).str.strip(),
            "measured_at": df["measured_at"],
            "value": to_number(df[num_cols[-1]]),
        })
    else:
        # Wide layout: one column per station
        long = df.melt(id_vars="measured_at", value_vars=num_cols,
                       var_name="station_id", value_name="value")
        long["value"] = to_number(long["value"])

    if PM_STATION_FILTER:
        long = long[long["station_id"].isin(PM_STATION_FILTER)]
    long = long.dropna(subset=["value"])
    long["measured_at"] = long["measured_at"].dt.strftime("%Y-%m-%d %H:%M:%S")
    return long


def read_knmi_file(path):
    df = pd.read_excel(path, sheet_name="Data")
    return pd.DataFrame({
        "station_id": KNMI_STATION_ID,
        "measured_at": pd.to_datetime(df["Timestamp (UTC)"]).dt.strftime("%Y-%m-%d %H:%M:%S"),
        "temperature": df["Temperature (°C)"],
        "humidity": df["Humidity (%)"],
        "pressure": df["Pressure (hPa)"],
    })


def already_imported(cur, path):
    cur.execute("SELECT 1 FROM ImportLog WHERE file_name = ?", (path.name,))
    return cur.fetchone() is not None


def import_pm_files(conn):
    cur = conn.cursor()
    for column, pattern in PM_PATTERNS.items():
        files = find_files(pattern)
        print(f"\n{column}: {len(files)} file(s) found")
        for path in files:
            if already_imported(cur, path):
                print(f"  {path.name}: already imported, skipped")
                continue
            try:
                data = read_pm_file(path)
            except Exception as err:
                print(f"  {path.name}: could not be read ({err})")
                continue

            cur.executemany(
                "INSERT OR IGNORE INTO Station (station_id, source, name) VALUES (?, 'PM dataset', ?)",
                [(s, s) for s in data["station_id"].unique()],
            )
            # Insert or fill in the PM column; the other PM column is kept
            sql = f"""
                INSERT INTO AirQualityHistory (station_id, measured_at, {column})
                VALUES (?, ?, ?)
                ON CONFLICT (station_id, measured_at)
                DO UPDATE SET {column} = excluded.{column}
            """
            rows = list(data[["station_id", "measured_at", "value"]].itertuples(index=False, name=None))
            for i in range(0, len(rows), BATCH_SIZE):
                cur.executemany(sql, rows[i:i + BATCH_SIZE])
            cur.execute("INSERT INTO ImportLog (file_name, rows_loaded) VALUES (?, ?)",
                        (path.name, len(rows)))
            conn.commit()
            print(f"  {path.name}: {len(rows):,} values from "
                  f"{data['station_id'].nunique()} station(s)")


def import_knmi_files(conn):
    cur = conn.cursor()
    files = find_files(KNMI_PATTERN)
    print(f"\nWeather: {len(files)} file(s) found")
    for path in files:
        if already_imported(cur, path):
            print(f"  {path.name}: already imported, skipped")
            continue
        data = read_knmi_file(path)
        cur.execute(
            "INSERT OR IGNORE INTO Station (station_id, source, name) VALUES (?, 'KNMI', ?)",
            (KNMI_STATION_ID, "Eindhoven Airport"),
        )
        cols = ["station_id", "measured_at", "temperature", "humidity", "pressure"]
        rows = list(data[cols].itertuples(index=False, name=None))
        cur.executemany(
            "INSERT OR REPLACE INTO WeatherHistory "
            "(station_id, measured_at, temperature, humidity, pressure) VALUES (?, ?, ?, ?, ?)",
            rows,
        )
        cur.execute("INSERT INTO ImportLog (file_name, rows_loaded) VALUES (?, ?)",
                    (path.name, len(rows)))
        conn.commit()
        print(f"  {path.name}: {len(rows):,} hours")


# ---------- Queries ----------

def show_hourly_averages(cur):
    """Same kind of query the dashboard will use."""
    cur.execute("""
        SELECT s.sensor_id,
               a.name AS area,
               strftime('%Y-%m-%d %H:00', r.measured_at) AS hour,
               ROUND(AVG(r.pm_2_5), 1) AS avg_pm_2_5,
               ROUND(AVG(r.pm_10), 1)  AS avg_pm_10
        FROM Reading r
        JOIN Sensor s ON s.sensor_id = r.sensor_id
        JOIN Area   a ON a.area_id   = s.area_id
        GROUP BY s.sensor_id, hour
        ORDER BY hour
    """)
    print("\nHourly averages (own sensors):")
    print("sensor | area     | hour             | PM2.5 | PM10")
    for row in cur.fetchall():
        print(f"{row[0]:>6} | {row[1]:<8} | {row[2]} | {row[3]:>5} | {row[4]:>4}")


def show_dataset_summary(cur):
    print("\nOpen datasets in the database:")
    cur.execute("""
        SELECT COUNT(DISTINCT station_id), COUNT(*), MIN(measured_at), MAX(measured_at),
               SUM(pm_2_5 IS NOT NULL), SUM(pm_10 IS NOT NULL)
        FROM AirQualityHistory
    """)
    st, n, first, last, n25, n10 = cur.fetchone()
    print(f"  PM:      {st or 0} station(s), {n:,} hours, {first} to {last} "
          f"(PM2.5 values: {n25 or 0:,}, PM10 values: {n10 or 0:,})")
    cur.execute("SELECT COUNT(*), MIN(measured_at), MAX(measured_at) FROM WeatherHistory")
    n, first, last = cur.fetchone()
    print(f"  Weather: {n:,} hours, {first} to {last}")


def training_data(conn, station_id):
    """
    PM and weather for one PM station, matched per hour.
    This is the table the AI model can train on.
    """
    query = """
        SELECT aq.measured_at, aq.pm_2_5, aq.pm_10,
               w.temperature, w.humidity, w.pressure
        FROM AirQualityHistory aq
        LEFT JOIN WeatherHistory w
               ON w.measured_at = aq.measured_at AND w.station_id = ?
        WHERE aq.station_id = ?
        ORDER BY aq.measured_at
    """
    return pd.read_sql_query(query, conn, params=(KNMI_STATION_ID, station_id))


def main():
    conn = sqlite3.connect(DB_FILE)
    conn.execute("PRAGMA foreign_keys = ON")
    cur = conn.cursor()
    try:
        create_tables(cur)
        insert_example_data(cur)
        conn.commit()
        import_pm_files(conn)
        import_knmi_files(conn)
        show_hourly_averages(cur)
        show_dataset_summary(cur)
        print(f"\nDone. Database saved in: {DB_FILE}")
    except sqlite3.Error as err:
        conn.rollback()
        print(f"Something went wrong: {err}")
    finally:
        conn.close()


if __name__ == "__main__":
    main()