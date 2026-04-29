import os
import struct

def convert_bmp_to_header(bmp_path, header_path):
    with open(bmp_path, 'rb') as f:
        # Check BMP header
        header = f.read(54)
        if header[:2] != b'BM':
            print("Not a valid BMP file")
            return

        data_offset = struct.unpack('<I', header[10:14])[0]
        width = struct.unpack('<i', header[18:22])[0]
        height = struct.unpack('<i', header[22:26])[0]
        bpp = struct.unpack('<H', header[28:30])[0]

        if bpp != 24:
            print(f"Only 24-bit BMP supported, found {bpp}")
            return

        print(f"Converting {bmp_path} ({width}x{height}, {bpp}bpp)")

        f.seek(data_offset)
        
        # BMP rows are padded to 4-byte boundaries
        row_size = (width * 3 + 3) & ~3
        
        pixels_rgb565 = []
        
        # BMP stores rows bottom-up
        rows = []
        for y in range(abs(height)):
            row_data = f.read(row_size)
            rows.append(row_data)
        
        # Flip if height is positive (standard BMP)
        if height > 0:
            rows.reverse()
            
        for row_data in rows:
            for x in range(width):
                b, g, r = row_data[x*3], row_data[x*3+1], row_data[x*3+2]
                
                # Convert to RGB565
                # r (5 bits), g (6 bits), b (5 bits)
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Big-endian swap (as done in Display.cpp via __builtin_bswap16)
                rgb565_swapped = ((rgb565 & 0xFF) << 8) | (rgb565 >> 8)
                pixels_rgb565.append(rgb565_swapped)

    # Write to header file
    with open(header_path, 'w') as f:
        f.write("#pragma once\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"// Embedded splash image: {width}x{abs(height)} RGB565 (Big Endian)\n")
        f.write(f"const uint16_t splash_width = {width};\n")
        f.write(f"const uint16_t splash_height = {abs(height)};\n")
        f.write("const uint16_t splash_image_data[] = {\n")
        
        for i, p in enumerate(pixels_rgb565):
            f.write(f"0x{p:04X},")
            if (i + 1) % 16 == 0:
                f.write("\n")
        
        f.write("\n};\n")
    
    print(f"Header written to {header_path}")

if __name__ == "__main__":
    convert_bmp_to_header("spiffs/splash.bmp", "src/display/splash_image.h")
