#!/usr/bin/env python3
"""Package timestamped, unaltered browser captures as a review video."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    args = parser.parse_args()
    directory = args.input.resolve(strict=True)
    report_path = directory / "report.json"
    report_hash = sha(report_path)
    report = json.loads(report_path.read_text())
    assert report["status"] == "captured; visual review required"
    frames = report["frames"]
    assert 30 <= len(frames) <= report["recipe"]["frame_capacity"]
    seconds = report["recipe"]["duration_seconds"]
    timestamps = []
    for frame in frames:
        assert re.fullmatch(r"frame-[0-9]{4}\.jpg", frame["filename"])
        assert sha(directory / frame["filename"]) == frame["sha256"]
        assert 0 <= frame["before_seconds"] <= frame["after_seconds"]
        timestamps.append((frame["before_seconds"] + frame["after_seconds"]) / 2)
    assert all(b > a for a, b in zip(timestamps, timestamps[1:]))
    assert timestamps[0] < 1 and seconds - 1 < timestamps[-1] < seconds + 1
    timestamps[0] = 0  # Hold the first sampled near view from the start.
    timestamps.append(max(seconds, timestamps[-1] + 1 / 12))
    concat = directory / "frames.ffconcat"
    video = directory / "motion.webm"
    manifest = directory / "video.json"
    assert not video.exists() and not manifest.exists(), "Use a fresh encoding output"
    lines = ["ffconcat version 1.0"]
    for index, frame in enumerate(frames):
        lines += ["file '" + frame["filename"] + "'",
                  f"duration {timestamps[index + 1] - timestamps[index]:.9f}"]
    lines.append("file '" + frames[-1]["filename"] + "'")
    concat.write_text("\n".join(lines) + "\n")
    command = ["ffmpeg", "-nostdin", "-n", "-hide_banner", "-loglevel", "warning",
               "-f", "concat", "-safe", "1", "-i", str(concat), "-an",
               "-vf", "fps=12", "-t", str(seconds), "-c:v", "libvpx-vp9",
               "-crf", "26", "-b:v", "0", "-cpu-used", "4", "-row-mt", "1",
               "-threads", "8", "-pix_fmt", "yuv420p", str(video)]
    metadata = {"status": "running", "source_report_sha256": report_hash,
                "runner_sha256": sha(Path(__file__)), "argv": command,
                "scope": "Camera advances continuously in the real application. Video holds captured frames at their measured midpoint times; 12 FPS encoding does not imply 12 unique captures per second or game performance.",
                "unique_captures": len(frames), "requested_seconds": seconds,
                "sample_midpoints_seconds": timestamps,
                "concat_sha256": sha(concat),
                "ffmpeg_version": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0]}
    try:
        subprocess.run(command, check=True, timeout=240)
        probe = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-count_frames", "-show_streams", "-show_format",
            "-of", "json", str(video)], text=True))
        stream = probe["streams"][0]
        assert [stream["width"], stream["height"]] == report["recipe"]["physical_viewport"]
        assert stream["codec_name"] == "vp9" and stream["r_frame_rate"] == "12/1"
        assert int(stream["nb_read_frames"]) == seconds * 12
        assert abs(float(probe["format"]["duration"]) - seconds) < .1
        assert sha(report_path) == report_hash
        metadata.update(status="encoded and decoded", probe=probe, video_sha256=sha(video))
    except Exception as error:
        metadata.update(status="failed", error=str(error))
        raise
    finally:
        manifest.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
