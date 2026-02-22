
import os
import sys
from PIL import Image

def convert_to_jpg(input_path, output_path, quality=90):
    try:
        print(f"Opening {input_path}...")
        # Increase limit for large images
        Image.MAX_IMAGE_PIXELS = None
        
        with Image.open(input_path) as img:
            print(f"Image format: {img.format}, Size: {img.size}, Mode: {img.mode}")
            
            # Convert to RGB (remove alpha) for JPEG
            if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
                bg = Image.new('RGB', img.size, (255, 255, 255))
                if img.mode != 'RGBA':
                    img = img.convert('RGBA')
                bg.paste(img, mask=img.split()[3])
                img = bg
            elif img.mode != 'RGB':
                img = img.convert('RGB')

            print(f"Saving to {output_path} with quality {quality}...")
            img.save(output_path, 'JPEG', quality=quality)
            print("Done.")
            
            # Print file size comparison
            in_size = os.path.getsize(input_path) / (1024*1024)
            out_size = os.path.getsize(output_path) / (1024*1024)
            print(f"Size: {in_size:.2f} MB -> {out_size:.2f} MB")
            
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 process_assets.py <input> <output> [quality]")
        sys.exit(1)
        
    in_path = sys.argv[1]
    out_path = sys.argv[2]
    q = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    
    convert_to_jpg(in_path, out_path, q)
