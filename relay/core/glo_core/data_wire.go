package glocore

import (
	"encoding/binary"
	"errors"
)

// GLOD1 is the plaintext gameplay data-plane wire format. It intentionally
// carries game payload bytes without encryption, compression, inspection or
// transformation. Session establishment and all control traffic remain on GLO6.
const (
	DataVersion       = 1
	DataHeaderSize    = 32
	DataRouteMetaSize = 8
	DataMaxPayload    = MaxInnerUDPPayload
	DataMaxDatagram   = DataHeaderSize + DataRouteMetaSize + DataMaxPayload
)

var DataMagic = [4]byte{'G', 'L', 'O', 'D'}

type DataDirection uint8

const (
	DataDirectionC2S DataDirection = 1
	DataDirectionS2C DataDirection = 2
)

type DataFrame struct {
	Direction DataDirection
	SessionID uint64
	Sequence  uint64
	FlowID    uint32
	Endpoint  []byte // exactly 8 bytes for C2S, empty for S2C; aliases input on decode
	Payload   []byte // raw game UDP payload; aliases input on decode
}

func EncodeDataInto(dst []byte, frame DataFrame) ([]byte, error) {
	metaLen := 0
	switch frame.Direction {
	case DataDirectionC2S:
		if len(frame.Endpoint) != DataRouteMetaSize {
			return nil, errors.New("bad C2S route metadata")
		}
		metaLen = DataRouteMetaSize
	case DataDirectionS2C:
		if len(frame.Endpoint) != 0 {
			return nil, errors.New("unexpected S2C route metadata")
		}
	default:
		return nil, errors.New("bad data direction")
	}
	if frame.SessionID == 0 || frame.Sequence == 0 || frame.FlowID == 0 || len(frame.Payload) > DataMaxPayload {
		return nil, errors.New("invalid data frame")
	}
	total := DataHeaderSize + metaLen + len(frame.Payload)
	if cap(dst) < total {
		dst = make([]byte, total)
	} else {
		dst = dst[:total]
		clear(dst[:DataHeaderSize])
	}
	copy(dst[0:4], DataMagic[:])
	dst[4] = DataVersion
	dst[5] = byte(frame.Direction)
	// 6..7 flags = 0
	binary.BigEndian.PutUint64(dst[8:16], frame.SessionID)
	binary.BigEndian.PutUint64(dst[16:24], frame.Sequence)
	binary.BigEndian.PutUint32(dst[24:28], frame.FlowID)
	binary.BigEndian.PutUint16(dst[28:30], uint16(len(frame.Payload)))
	dst[30] = byte(metaLen)
	dst[31] = 0
	off := DataHeaderSize
	if metaLen != 0 {
		copy(dst[off:off+metaLen], frame.Endpoint)
		off += metaLen
	}
	copy(dst[off:], frame.Payload)
	return dst, nil
}

// DecodeDataView validates GLOD1 without copying. Endpoint and Payload alias b.
func DecodeDataView(b []byte) (DataFrame, error) {
	if len(b) < DataHeaderSize || len(b) > DataMaxDatagram {
		return DataFrame{}, errors.New("bad data size")
	}
	if b[0] != DataMagic[0] || b[1] != DataMagic[1] || b[2] != DataMagic[2] || b[3] != DataMagic[3] || b[4] != DataVersion {
		return DataFrame{}, errors.New("bad data magic/version")
	}
	if b[6] != 0 || b[7] != 0 || b[31] != 0 {
		return DataFrame{}, errors.New("reserved data bits set")
	}
	direction := DataDirection(b[5])
	metaLen := int(b[30])
	switch direction {
	case DataDirectionC2S:
		if metaLen != DataRouteMetaSize {
			return DataFrame{}, errors.New("bad C2S metadata size")
		}
	case DataDirectionS2C:
		if metaLen != 0 {
			return DataFrame{}, errors.New("bad S2C metadata size")
		}
	default:
		return DataFrame{}, errors.New("bad data direction")
	}
	payloadLen := int(binary.BigEndian.Uint16(b[28:30]))
	if payloadLen > DataMaxPayload || len(b) != DataHeaderSize+metaLen+payloadLen {
		return DataFrame{}, errors.New("bad data payload size")
	}
	sid := binary.BigEndian.Uint64(b[8:16])
	seq := binary.BigEndian.Uint64(b[16:24])
	fid := binary.BigEndian.Uint32(b[24:28])
	if sid == 0 || seq == 0 || fid == 0 {
		return DataFrame{}, errors.New("zero data identity")
	}
	off := DataHeaderSize
	endpoint := b[off : off+metaLen]
	off += metaLen
	return DataFrame{
		Direction: direction,
		SessionID: sid,
		Sequence:  seq,
		FlowID:    fid,
		Endpoint:  endpoint,
		Payload:   b[off:],
	}, nil
}
