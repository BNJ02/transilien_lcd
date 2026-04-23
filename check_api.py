#!/usr/bin/env python3
"""Fetch the next 3 hours for selected departures around Bièvres.

Targets:
- Transilien V direction Massy-Palaiseau from Bièvres station
- Bus 4615 direction Vélizy 2 from Mairie de Bièvres
- Bus 6133 direction Gare de Chaville Rive Droite from Mairie de Bièvres
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from zoneinfo import ZoneInfo


DEFAULT_API_KEY = ""  # set via env: export PRIM_API_KEY=your_key
NAVITIA_BASE = "https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia"
PARIS_TZ = ZoneInfo("Europe/Paris")


@dataclass(frozen=True)
class Target:
    label: str
    stop_name: str
    stop_id: str
    line_label: str
    direction_prefix: str


TARGETS = [
    Target(
        label="Transilien V -> Massy-Palaiseau",
        stop_name="Bièvres",
        stop_id="stop_area:IDFM:63404",
        line_label="V",
        direction_prefix="Massy - Palaiseau",
    ),
    Target(
        label="4615 -> Vélizy 2",
        stop_name="Mairie de Bièvres",
        stop_id="stop_area:IDFM:63415",
        line_label="4615",
        direction_prefix="Vélizy 2",
    ),
    Target(
        label="6133 -> Gare de Chaville Rive Droite",
        stop_name="Mairie de Bièvres",
        stop_id="stop_area:IDFM:63415",
        line_label="6133",
        direction_prefix="Gare de Chaville Rive Droite",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch the next 3 hours for selected Bièvres departures."
    )
    parser.add_argument(
        "--api-key",
        default=os.getenv("PRIM_API_KEY", DEFAULT_API_KEY),
        help="PRIM API key (default: env PRIM_API_KEY then embedded value).",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=10800,
        help="Search window in seconds (default: 10800 = 3h).",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=60,
        help="Maximum departures fetched per stop area (default: 60).",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="HTTP timeout in seconds.",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Disable TLS certificate verification.",
    )
    return parser.parse_args()


def to_hhmm(raw_dt: str) -> str | None:
    if len(raw_dt) == 15 and raw_dt[8] == "T":
        return f"{raw_dt[9:11]}:{raw_dt[11:13]}"
    try:
        dt = datetime.fromisoformat(raw_dt.replace("Z", "+00:00"))
    except ValueError:
        return None
    if dt.tzinfo is None:
        return dt.strftime("%H:%M")
    return dt.astimezone(PARIS_TZ).strftime("%H:%M")


def make_request(url: str, api_key: str, timeout: float, context) -> dict:
    req = urllib.request.Request(
        url,
        headers={
            "apikey": api_key,
            "Accept": "application/json",
            "User-Agent": "transilien_lcd-multi-check/1.0",
        },
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=timeout, context=context) as resp:
        return json.load(resp)


def fetch_stop_departures(
    api_key: str,
    stop_id: str,
    duration: int,
    count: int,
    timeout: float,
    context,
) -> list[dict]:
    from_dt = datetime.now(PARIS_TZ).strftime("%Y%m%dT%H%M%S")
    stop_id_enc = urllib.parse.quote(stop_id, safe="")
    url = (
        f"{NAVITIA_BASE}/stop_areas/{stop_id_enc}/departures"
        f"?from_datetime={from_dt}&duration={duration}&count={count}"
    )
    payload = make_request(url, api_key=api_key, timeout=timeout, context=context)
    return payload.get("departures", [])


def filter_target_times(departures: list[dict], target: Target) -> list[str]:
    matches: list[str] = []
    want_direction = target.direction_prefix.lower()

    for dep in departures:
        info = dep.get("display_informations", {})
        line_label = str(info.get("label", "")).strip()
        if line_label != target.line_label:
            continue

        direction = str(info.get("direction", "")).strip()
        if not direction.lower().startswith(want_direction):
            continue

        raw_dt = dep.get("stop_date_time", {}).get("departure_date_time", "")
        hhmm = to_hhmm(raw_dt)
        if hhmm and hhmm not in matches:
            matches.append(hhmm)

    return matches


def main() -> int:
    args = parse_args()
    if not args.api_key:
        print("ERROR: missing API key (--api-key or PRIM_API_KEY)", file=sys.stderr)
        return 2

    context = ssl._create_unverified_context() if args.insecure else None

    print(f"Base Navitia: {NAVITIA_BASE}")
    print(f"Fenetre: {args.duration // 3600}h ({args.duration}s)")
    print(f"Heure locale: {datetime.now(PARIS_TZ).strftime('%Y-%m-%d %H:%M:%S %Z')}")

    cache: dict[str, list[dict]] = {}
    for target in TARGETS:
        if target.stop_id not in cache:
            cache[target.stop_id] = fetch_stop_departures(
                api_key=args.api_key,
                stop_id=target.stop_id,
                duration=args.duration,
                count=args.count,
                timeout=args.timeout,
                context=context,
            )

    print()
    for target in TARGETS:
        times = filter_target_times(cache[target.stop_id], target)
        print(target.label)
        print(f"  Arret: {target.stop_name}")
        print(f"  Horaires: {' | '.join(times) if times else 'none'}")
        print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
