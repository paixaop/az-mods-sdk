/* SPDX-License-Identifier: GPL-3.0-only */
/* Timing model for the synthetic harness provider, matching the core contract. */
#include <math.h>
#include "azmod-timestamps.h"
/* The synthetic stored grid follows the native ANLZ millisecond clock. */
/* No extrapolation beyond the captured grid; no constant-tempo or 4/4 assumption. */
static inline int ht_grid_request_valid(const az_track_snapshot *s, double value, const double *out) {
    return s && ht_record_size(s) >= sizeof *s && s->version == AZ_DATA_VERSION && out && isfinite(value);
}
static inline int ht_grid_request(const az_track_snapshot *s, double value, const double *out) {
    if (!ht_grid_request_valid(s, value, out)) return AZ_INVALID;
    if (s->beat_count && !s->beats) return AZ_INVALID;
    if (s->grid_info.availability != AZ_AVAIL_READY || s->beat_count < 2) return AZ_UNSUPPORTED;
    return AZ_OK;
}
static inline int ht_grid_bounds(const az_track_snapshot *s, double time, uint32_t rate, int *at_end) {
    const az_beat *b = s->beats;
    uint32_t hi = s->beat_count - 1;
    int extent = compare_timestamps(b[hi].time_s, b[0].time_s, 1, rate, 1);
    int start = compare_timestamps(time, b[0].time_s, 1, rate, 1);
    int end = compare_timestamps(time, b[hi].time_s, 1, rate, 1);
    if (extent == AZ_TIME_INVALID || extent != AZ_TIME_AFTER || start == AZ_TIME_INVALID || end == AZ_TIME_INVALID) return AZ_FAILED;
    if (start == AZ_TIME_BEFORE || end == AZ_TIME_AFTER) return AZ_NOT_FOUND;
    *at_end = end == AZ_TIME_EQUAL;
    return AZ_OK;
}
static inline int ht_grid_middle(const az_beat *b, uint32_t lo, uint32_t mid, uint32_t hi, double time, uint32_t rate) {
    int lower = compare_timestamps(b[mid].time_s, b[lo].time_s, 1, rate, 1);
    int upper = compare_timestamps(b[mid].time_s, b[hi].time_s, 1, rate, 1);
    int target = compare_timestamps(b[mid].time_s, time, 1, rate, 1);
    if (lower == AZ_TIME_INVALID || upper == AZ_TIME_INVALID || target == AZ_TIME_INVALID ||
        lower != AZ_TIME_AFTER || upper != AZ_TIME_BEFORE) return AZ_TIME_INVALID;
    return target;
}
static inline int ht_grid_interpolate(const az_beat *b, uint32_t lo, uint32_t hi, double time, uint32_t rate, double *out) {
    double span = b[hi].time_s - b[lo].time_s;
    int order = compare_timestamps(span, 0.0, 1, rate, 1);
    if (order == AZ_TIME_INVALID || order != AZ_TIME_AFTER) return AZ_FAILED;
    double value = lo + (time - b[lo].time_s) / span;
    if (!isfinite(value)) return AZ_FAILED;
    *out = value;
    return AZ_OK;
}
static inline int ht_grid_search(const az_track_snapshot *s, double time, double *out, uint32_t rate) {
    uint32_t lo = 0, hi = s->beat_count - 1;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        int target = ht_grid_middle(s->beats, lo, mid, hi, time, rate);
        if (target == AZ_TIME_INVALID) return AZ_FAILED;
        if (target == AZ_TIME_BEFORE || target == AZ_TIME_EQUAL) lo = mid;
        else hi = mid;
    }
    return ht_grid_interpolate(s->beats, lo, hi, time, rate, out);
}
static inline int ht_time_to_beat_rate(const az_track_snapshot *s, double time, double *out, uint32_t source_rate) {
    if (!source_rate) return AZ_INVALID;
    int rc = ht_grid_request(s, time, out);
    if (rc != AZ_OK) return rc;
    int at_end;
    rc = ht_grid_bounds(s, time, source_rate, &at_end);
    if (rc != AZ_OK) return rc;
    if (at_end) { *out = s->beat_count - 1; return AZ_OK; }
    return ht_grid_search(s, time, out, source_rate);
}
static inline int ht_time_to_beat(const az_track_snapshot *s, double time, double *out) {
    return ht_time_to_beat_rate(s, time, out, 1000); /* encoded FILE milliseconds */
}
static inline int ht_grid_beat_coordinate(const az_track_snapshot *s, double beat, double *out) {
    uint32_t i = (uint32_t) beat;
    uint32_t lo = i + 1 < s->beat_count ? i : i - 1;
    double first = s->beats[lo].time_s, last = s->beats[lo + 1].time_s;
    double span = last - first;
    int span_order = compare_timestamps(last, first, 1, 1000, 1);
    if (span_order == AZ_TIME_INVALID || span_order != AZ_TIME_AFTER || !isfinite(span)) return AZ_FAILED;
    double value = i == lo ? first + (beat - i) * span : last;
    if (!isfinite(value)) return AZ_FAILED;
    *out = value;
    return AZ_OK;
}
static inline int ht_beat_to_time(const az_track_snapshot *s, double beat, double *out) {
    int rc = ht_grid_request(s, beat, out);
    if (rc != AZ_OK) return rc;
    if (beat < 0 || beat > s->beat_count - 1) return AZ_NOT_FOUND;
    return ht_grid_beat_coordinate(s, beat, out);
}
static inline int ht_interval_request_valid(const az_track_snapshot *s, double rate, const az_time_interval *out) {
    if (s && (ht_record_size(s) < sizeof *s || s->version != AZ_DATA_VERSION)) return 0;
    return out && ht_record_size(out) >= sizeof *out && isfinite(rate) && rate >= 0;
}
static inline int ht_interval_order(double a, double b) {
    int start = compare_timestamps(a, 0.0, 1, 1000, 1);
    int source = compare_timestamps(b, a, 1, 1000, 1);
    if (start == AZ_TIME_INVALID || source == AZ_TIME_INVALID ||
        start == AZ_TIME_BEFORE || source == AZ_TIME_BEFORE) return AZ_TIME_INVALID;
    return source;
}
static inline void ht_interval_beats(const az_track_snapshot *s, double a, double b, int order, az_time_interval *v) {
    double x, y;
    if (ht_time_to_beat(s, a, &x) == AZ_OK && ht_time_to_beat(s, b, &y) == AZ_OK) {
        v->beats = order == AZ_TIME_EQUAL ? 0 : y - x;
        v->valid |= AZ_INTERVAL_BEATS;
    }
}
static inline void ht_interval_playback(double rate, int order, az_time_interval *v) {
    double playback = rate > 0 ? v->source_s / rate : 0;
    /* FP_ZERO detects representation underflow after division; timestamp
     * equality and ordering were already decided by compare_timestamps. */
    if (rate > 0 && isfinite(playback) && (order == AZ_TIME_EQUAL || fpclassify(playback) != FP_ZERO)) {
        v->playback_rate = rate;
        v->playback_s = playback;
        v->valid |= AZ_INTERVAL_PLAYBACK;
    }
}
static inline int ht_interval(const az_track_snapshot *s, double a, double b, double rate, az_time_interval *out) {
    if (!ht_interval_request_valid(s, rate, out)) return AZ_INVALID;
    int source_order = ht_interval_order(a, b);
    if (source_order == AZ_TIME_INVALID) return AZ_INVALID;
    az_time_interval v = {0};
    v.size = out->size;
    v.version = AZ_DATA_VERSION;
    v.valid = AZ_INTERVAL_SOURCE;
    v.source_s = source_order == AZ_TIME_EQUAL ? 0 : b - a;
    ht_interval_beats(s, a, b, source_order, &v);
    ht_interval_playback(rate, source_order, &v);
    memcpy(out, &v, sizeof v);
    return AZ_OK;
}
