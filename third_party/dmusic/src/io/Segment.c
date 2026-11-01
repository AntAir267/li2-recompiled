// Copyright © 2024. GothicKit Contributors
// SPDX-License-Identifier: MIT-Modern-Variant
#include "_Internal.h"

#include <math.h>

static DmResult DmSegment_parseTempoTrack(DmMessageList* slf, DmRiff* rif) {
	uint32_t item_size = 0;
	DmRiff_readDword(rif, &item_size);

	DmMessage msg;
	uint32_t item_count = (rif->len - rif->pos) / item_size;
	for (uint32_t i = 0; i < item_count; ++i) {
		uint32_t end_position = rif->pos + item_size;

		memset(&msg, 0, sizeof msg);
		msg.type = DmMessage_TEMPO;

		DmRiff_readDword(rif, &msg.time);

		uint32_t pad;
		DmRiff_readDword(rif, &pad);

		DmRiff_readDouble(rif, &msg.tempo.tempo);

		DmResult rv = DmMessageList_add(slf, msg);
		if (rv != DmResult_SUCCESS) {
			return rv;
		}

		rif->pos = end_position;
	}

	return DmResult_SUCCESS;
}

static void DmSegment_parseCommandItem(DmMessage_Command* slf, DmRiff* rif) {
	memset(slf, 0, sizeof *slf);
	slf->type = DmMessage_COMMAND;

	DmRiff_readDword(rif, &slf->time);
	DmRiff_readWord(rif, &slf->measure);
	DmRiff_readByte(rif, &slf->beat);

	uint8_t tmp = 0;
	DmRiff_readByte(rif, &tmp);
	slf->command = tmp;

	DmRiff_readByte(rif, &slf->groove_level);
	DmRiff_readByte(rif, &slf->groove_range);

	DmRiff_readByte(rif, &tmp);
	slf->repeat_mode = tmp;

	// Groove range values above 100 are not allowed. Set it to 0 instead.
	if (slf->groove_range > 100) {
		slf->groove_range = 0;
	}

	// If the groove rand is odd, it is adjusted down.
	// See https://documentation.help/DirectMusic/dmusiocommand.htm
	if ((slf->groove_range % 2) != 0) {
		slf->groove_range -= 1;
	}

	// In DirectMusic versions before DirectX 8, the repetition mode was always `RANDOM`, thus
	// this byte is just padding. Just set it to `RANDOM` if it exceeds the maximum allowed value.
	if (slf->repeat_mode > DmPatternSelect_RANDOM_ROW) {
		slf->repeat_mode = DmPatternSelect_RANDOM;
	}
}

static DmResult DmSegment_parseCommandTrack(DmMessageList* slf, DmRiff* rif) {
	uint32_t item_size = 0;
	DmRiff_readDword(rif, &item_size);

	DmMessage msg;
	uint32_t item_count = (rif->len - rif->pos) / item_size;
	for (uint32_t i = 0; i < item_count; ++i) {
		uint32_t end_position = rif->pos + item_size;

		DmSegment_parseCommandItem(&msg.command, rif);

		DmResult rv = DmMessageList_add(slf, msg);
		if (rv != DmResult_SUCCESS) {
			return rv;
		}

		rif->pos = end_position;
	}

	return DmResult_SUCCESS;
}

static void DmSegment_parseChordItem(DmMessage_Chord* slf, DmRiff* rif) {
	memset(slf, 0, sizeof *slf);
	slf->type = DmMessage_CHORD;

	uint32_t item_size = 0;
	DmRiff_readDword(rif, &item_size);

	// First, parse DMUS_IO_CHORD.
	{
		uint32_t end_position = rif->pos + item_size;

		uint16_t name[16];
		DmRiff_read(rif, name, 16 * sizeof(int16_t));
		(void) Dm_utf16ToUtf8Inline(slf->name, name);

		DmRiff_readDword(rif, &slf->time);
		DmRiff_readWord(rif, &slf->measure);
		DmRiff_readByte(rif, &slf->beat);

		uint8_t silent = 0;
		DmRiff_readByte(rif, &silent);
		slf->silent = silent == 1;

		rif->pos = end_position;
	}

	// Second, parse a list of sub-chords (4 max.)
	DmRiff_readDword(rif, &slf->subchord_count);

	uint32_t max_subchord_count = (sizeof slf->subchords) / (sizeof *slf->subchords);
	if (slf->subchord_count > max_subchord_count) {
		Dm_report(DmLogLevel_WARN,
		          "DmMessage: Chord message reports too many sub-chords: got %d, expected at maximum %d",
		          slf->subchord_count,
		          max_subchord_count);
		slf->subchord_count = max_subchord_count;
	}

	DmRiff_readDword(rif, &item_size);

	for (uint32_t i = 0; i < slf->subchord_count; ++i) {
		uint32_t end_position = rif->pos + item_size;

		DmRiff_readDword(rif, &slf->subchords[i].chord_pattern);
		DmRiff_readDword(rif, &slf->subchords[i].scale_pattern);
		DmRiff_readDword(rif, &slf->subchords[i].inversion_points);
		DmRiff_readDword(rif, &slf->subchords[i].levels);
		DmRiff_readByte(rif, &slf->subchords[i].chord_root);
		DmRiff_readByte(rif, &slf->subchords[i].scale_root);

		rif->pos = end_position;
	}
}

static DmResult DmSegment_parseChordTrack(DmMessageList* slf, DmRiff* rif) {
	uint32_t crdh = 0;

	DmMessage msg;
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_CRDH, 0)) {
			DmRiff_readDword(&cnk, &crdh);
		} else if (DmRiff_is(&cnk, DM_FOURCC_CRDB, 0)) {
			DmSegment_parseChordItem(&msg.chord, &cnk);

			DmResult rv = DmMessageList_add(slf, msg);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseBandItem(DmMessage_Band* slf, DmRiff* rif) {
	memset(slf, 0, sizeof *slf);
	slf->type = DmMessage_BAND;

	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_BDIH, 0)) {
			DmRiff_readDword(&cnk, &slf->time);
		} else if (DmRiff_is(&cnk, DM_FOURCC_RIFF, DM_FOURCC_DMBD)) {
			DmResult rv = DmBand_create(&slf->band);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}

			rv = DmBand_parse(slf->band, &cnk);
			if (rv != DmResult_SUCCESS) {
				DmBand_release(slf->band);
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseBandList(DmMessageList* slf, DmRiff* rif) {
	DmMessage msg;
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_LBND)) {
			DmResult rv = DmSegment_parseBandItem(&msg.band, &cnk);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}

			rv = DmMessageList_add(slf, msg);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseBandTrack(DmMessageList* slf, DmRiff* rif) {
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_LBDL)) {
			DmResult rv = DmSegment_parseBandList(slf, &cnk);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

static void DmSegment_parseStyleItem(DmMessage_Style* slf, DmRiff* rif) {
	memset(slf, 0, sizeof *slf);
	slf->type = DmMessage_STYLE;

	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_STMP, 0)) {
			DmRiff_readDword(&cnk, &slf->time);
		} else if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_DMRF)) {
			DmReference_parse(&slf->reference, &cnk);
		}

		DmRiff_reportDone(&cnk);
	}
}

static DmResult DmSegment_parseStyleTrack(DmMessageList* slf, DmRiff* rif) {
	DmMessage msg;
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_STRF)) {
			DmSegment_parseStyleItem(&msg.style, &cnk);

			DmResult rv = DmMessageList_add(slf, msg);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}


// li2-rip: sequence tracks ('seqt'), the plain MIDI-style tracks that DirectMusic Producer writes for
// sequenced (non-style) music. Layouts follow DMUS_IO_SEQ_ITEM and DMUS_IO_CURVE_ITEM from the DX8 SDK.
#define DmInt_SEQ_CURVE_SPACING 10

static uint32_t DmSegment_seqTime(int32_t time, int16_t offset) {
	int32_t t = time + offset;
	return t < 0 ? 0 : (uint32_t) t;
}

static DmResult DmSegment_parseSequenceEvents(DmMessageList* slf, DmRiff* rif) {
	uint32_t item_size = 0;
	DmRiff_readDword(rif, &item_size);
	if (item_size < 17) {
		return DmResult_SUCCESS;
	}

	uint32_t item_count = (rif->len - rif->pos) / item_size;
	for (uint32_t i = 0; i < item_count; ++i) {
		uint32_t end_position = rif->pos + item_size;

		int32_t time, duration;
		uint32_t pchannel;
		int16_t offset;
		uint8_t status, b1, b2;
		DmRiff_readInt(rif, &time);
		DmRiff_readInt(rif, &duration);
		DmRiff_readDword(rif, &pchannel);
		DmRiff_readShort(rif, &offset);
		DmRiff_readByte(rif, &status);
		DmRiff_readByte(rif, &b1);
		DmRiff_readByte(rif, &b2);
		rif->pos = end_position;

		uint32_t t = DmSegment_seqTime(time, offset);
		DmMessage msg;
		memset(&msg, 0, sizeof msg);
		DmResult rv = DmResult_SUCCESS;

		switch (status & 0xF0) {
		case 0x90:
			if (b2 == 0) {
				break;
			}
			msg.type = DmMessage_NOTE;
			msg.time = t;
			msg.note.on = true;
			msg.note.note = b1;
			msg.note.velocity = b2;
			msg.note.channel = pchannel;
			rv = DmMessageList_add(slf, msg);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
			// End one tick early so a repeated note on the same pitch is not cut by this note-off.
			msg.time = t + (uint32_t) (duration > 1 ? duration - 1 : 1);
			msg.note.on = false;
			rv = DmMessageList_add(slf, msg);
			break;
		case 0xB0:
			msg.type = DmMessage_CONTROL;
			msg.time = t;
			msg.control.control = b1;
			msg.control.value = (float) b2 / 127.f;
			msg.control.channel = pchannel;
			rv = DmMessageList_add(slf, msg);
			break;
		case 0xE0:
			msg.type = DmMessage_PITCH_BEND;
			msg.time = t;
			msg.pitch_bend.channel = pchannel;
			msg.pitch_bend.value = b1 | (b2 << 7);
			rv = DmMessageList_add(slf, msg);
			break;
		default:
			Dm_report(DmLogLevel_DEBUG, "DmSegment: Sequence event with status 0x%02x ignored", status);
			break;
		}

		if (rv != DmResult_SUCCESS) {
			return rv;
		}
	}

	return DmResult_SUCCESS;
}

static float DmSegment_curveValue(uint8_t shape, float phase, float a, float b) {
	switch (shape) {
	case 1: // instant
		return b;
	case 2: // exp
		phase = powf(phase, 4);
		break;
	case 3: // log
		phase = sqrtf(phase);
		break;
	case 4: // sine
		phase = (sinf((phase - 0.5f) * 3.14159265f) + 1) * 0.5f;
		break;
	default:
		break;
	}
	return a + (b - a) * phase;
}

static DmResult DmSegment_parseSequenceCurves(DmMessageList* slf, DmRiff* rif) {
	uint32_t item_size = 0;
	DmRiff_readDword(rif, &item_size);
	if (item_size < 28) {
		return DmResult_SUCCESS;
	}

	uint32_t item_count = (rif->len - rif->pos) / item_size;
	for (uint32_t i = 0; i < item_count; ++i) {
		uint32_t end_position = rif->pos + item_size;

		int32_t start, duration, reset_duration;
		uint32_t pchannel;
		int16_t offset, start_value, end_value, reset_value;
		uint8_t type, shape, cc, flags;
		DmRiff_readInt(rif, &start);
		DmRiff_readInt(rif, &duration);
		DmRiff_readInt(rif, &reset_duration);
		DmRiff_readDword(rif, &pchannel);
		DmRiff_readShort(rif, &offset);
		DmRiff_readShort(rif, &start_value);
		DmRiff_readShort(rif, &end_value);
		DmRiff_readShort(rif, &reset_value);
		DmRiff_readByte(rif, &type);
		DmRiff_readByte(rif, &shape);
		DmRiff_readByte(rif, &cc);
		DmRiff_readByte(rif, &flags);
		rif->pos = end_position;

		bool is_cc = type == DmCurveType_CONTROL_CHANGE;
		bool is_pb = type == DmCurveType_PITCH_BEND;
		if (!is_cc && !is_pb) {
			Dm_report(DmLogLevel_DEBUG, "DmSegment: Sequence curve type %d ignored", type);
			continue;
		}
		if (is_cc && (start_value < 0 || start_value > 127 || end_value < 0 || end_value > 127)) {
			continue;
		}

		uint32_t t0 = DmSegment_seqTime(start, offset);
		uint32_t len = duration > 0 && shape != 1 ? (uint32_t) duration : 0;
		int prev = -1;
		for (uint32_t k = 0;; k += DmInt_SEQ_CURVE_SPACING) {
			if (k > len) {
				k = len;
			}
			float phase = len ? (float) k / (float) len : 1.f;
			int value = (int) lrintf(DmSegment_curveValue(shape, phase, start_value, end_value));

			if (value != prev) {
				DmMessage msg;
				memset(&msg, 0, sizeof msg);
				msg.time = t0 + k;
				if (is_cc) {
					msg.type = DmMessage_CONTROL;
					msg.control.control = cc;
					msg.control.value = (float) value / 127.f;
					msg.control.channel = pchannel;
				} else {
					msg.type = DmMessage_PITCH_BEND;
					msg.pitch_bend.channel = pchannel;
					msg.pitch_bend.value = value;
				}
				DmResult rv = DmMessageList_add(slf, msg);
				if (rv != DmResult_SUCCESS) {
					return rv;
				}
				prev = value;
			}

			if (k >= len) {
				break;
			}
		}
	}

	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseSequenceTrack(DmMessageList* slf, DmRiff* rif) {
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		DmResult rv = DmResult_SUCCESS;
		if (DmRiff_is(&cnk, DM_FOURCC_EVTL, 0)) {
			rv = DmSegment_parseSequenceEvents(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_CURL, 0)) {
			rv = DmSegment_parseSequenceCurves(slf, &cnk);
		}
		if (rv != DmResult_SUCCESS) {
			return rv;
		}
	}
	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseTrack(DmMessageList* slf, DmRiff* rif) {
	DmGuid class_id;
	uint32_t position;
	uint32_t group;
	uint32_t chunk_id;
	uint32_t chunk_type;

	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		DmResult rv = DmResult_SUCCESS;
		if (DmRiff_is(&cnk, DM_FOURCC_TRKH, 0)) {
			DmGuid_parse(&class_id, &cnk);
			DmRiff_readDword(&cnk, &position);
			DmRiff_readDword(&cnk, &group);
			DmRiff_readDword(&cnk, &chunk_id);
			DmRiff_readDword(&cnk, &chunk_type);
		} else if (DmRiff_is(&cnk, DM_FOURCC_TETR, 0)) {
			rv = DmSegment_parseTempoTrack(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_CMND, 0)) {
			rv = DmSegment_parseCommandTrack(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_CORD)) {
			rv = DmSegment_parseChordTrack(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_STTR)) {
			rv = DmSegment_parseStyleTrack(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_RIFF, DM_FOURCC_DMBT)) {
			rv = DmSegment_parseBandTrack(slf, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_SEQT, 0)) {
			rv = DmSegment_parseSequenceTrack(slf, &cnk);
		}

		if (rv != DmResult_SUCCESS) {
			return rv;
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

static DmResult DmSegment_parseTrackList(DmMessageList* slf, DmRiff* rif) {
	DmRiff cnk;
	while (DmRiff_readChunk(rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_RIFF, DM_FOURCC_DMTK)) {
			DmResult rv = DmSegment_parseTrack(slf, &cnk);
			if (rv != DmResult_SUCCESS) {
				return rv;
			}
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}

DmResult DmSegment_parse(DmSegment* slf, void* buf, size_t len) {
	DmRiff rif;
	if (!DmRiff_init(&rif, buf, len)) {
		Dm_free(buf);
		return DmResult_FILE_CORRUPT;
	}

	slf->backing_memory = buf;

	DmRiff cnk;
	while (DmRiff_readChunk(&rif, &cnk)) {
		if (DmRiff_is(&cnk, DM_FOURCC_SEGH, 0)) {
			DmRiff_readDword(&cnk, &slf->repeats);
			DmRiff_readDword(&cnk, &slf->length);
			DmRiff_readDword(&cnk, &slf->play_start);
			DmRiff_readDword(&cnk, &slf->loop_start);
			DmRiff_readDword(&cnk, &slf->loop_end);
			DmRiff_readDword(&cnk, &slf->resolution);
		} else if (DmRiff_is(&cnk, DM_FOURCC_GUID, 0)) {
			DmGuid_parse(&slf->guid, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_VERS, 0)) {
			DmVersion_parse(&slf->version, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_UNFO)) {
			DmUnfo_parse(&slf->info, &cnk);
		} else if (DmRiff_is(&cnk, DM_FOURCC_LIST, DM_FOURCC_TRKL)) {
			DmSegment_parseTrackList(&slf->messages, &cnk);
		}

		DmRiff_reportDone(&cnk);
	}

	return DmResult_SUCCESS;
}
