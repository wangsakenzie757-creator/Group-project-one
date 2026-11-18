"""
Project OASIS – test version of the database using SQLite.

SQLite is built into Python: no server, no install, no login.
The whole database is saved in one file: oasis.db (next to this script).
Tables and relationships are the same as the MySQL version (ERD):
User, Area, Sensor, Reading.

Run:  python create_oasis_sqlite.py
"""

import sqlite3
from pathlib import Path

DB_FILE = Path(__file__).with_name("oasis.db")


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
}


def create_tables(cur):
    for name, ddl in TABLES.items():
        cur.execute(ddl)
        print(f"Table '{name}' is ready.")


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
    print("\nHourly averages:")
    print("sensor | area     | hour             | PM2.5 | PM10")
    for row in cur.fetchall():
        print(f"{row[0]:>6} | {row[1]:<8} | {row[2]} | {row[3]:>5} | {row[4]:>4}")


def main():
    conn = sqlite3.connect(DB_FILE)
    conn.execute("PRAGMA foreign_keys = ON")   
    cur = conn.cursor()
    try:
        create_tables(cur)
        insert_example_data(cur)   
        conn.commit()
        show_hourly_averages(cur)
        print(f"\nDone. Database saved in: {DB_FILE}")
    except sqlite3.Error as err:
        conn.rollback()
        print(f"Something went wrong: {err}")
    finally:
        conn.close()


if __name__ == "__main__":
    main()