package cobs

import (
	"io"
)

// StreamEncoder continuously reads packets from inputChan, encodes them 
// using standard COBS, and writes them separated by 0x00 frame delimiters 
// to the output io.Writer (representing a USB device file descriptor).
func StreamEncoder(inputChan <-chan []byte, output io.Writer) error {
	// Write an initial framing zero to perfectly synchronize the receiver, 
	// flushing any garbage bytes that might be lingering in its buffer.
	_, err := output.Write([]byte{0x00})
	if err != nil {
		return err
	}

	for packet := range inputChan {
		if len(packet) == 0 {
			panic("cobs: empty slices are not allowed")
		}

		encoded := Encode(packet)
		
		// Append the standard COBS frame delimiter (0x00)
		encoded = append(encoded, 0x00)

		// Write the fully encoded and framed packet to the USB device
		_, err := output.Write(encoded)
		if err != nil {
			return err
		}
	}
	return nil
}

// Encode performs standard Constant Overhead Byte Stuffing (COBS) on the input data.
// It returns the encoded payload without the framing zero.
func Encode(data []byte) []byte {
	// The maximum encoded length is len(data) + len(data)/254 + 1
	dst := make([]byte, 0, len(data)+len(data)/254+1)
	
	// Pre-allocate the first code byte placeholder
	dst = append(dst, 0)
	codeIdx := 0
	code := byte(1)
	
	for _, b := range data {
		if b == 0 {
			// Finish the current block by writing the distance
			dst[codeIdx] = code
			// Start a new block with a new code byte placeholder
			dst = append(dst, 0)
			codeIdx = len(dst) - 1
			code = 1
		} else {
			// Copy data byte and increment distance
			dst = append(dst, b)
			code++
			
			// If block reaches maximum length of 254 data bytes
			if code == 0xFF {
				// Finish the current block
				dst[codeIdx] = code
				// Start a new block
				dst = append(dst, 0)
				codeIdx = len(dst) - 1
				code = 1
			}
		}
	}
	
	// Write the final block's code byte
	dst[codeIdx] = code
	
	return dst
}
