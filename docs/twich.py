import os
import numpy as np
from PIL import Image

def create_twitch_gif(image_path, output_path="twitched_ears.gif"):
    # 1. Load and prepare the base image
    try:
        base_img = Image.open(image_path).convert("RGBA")
    except Exception as e:
        print(f"Error loading image: {e}")
        print("Please ensure the image path is correct and the file exists.")
        return

    width, height = base_img.size
    img_array = np.array(base_img)
    
    frames = []
    num_frames = 24  # 1-second loop at 24fps
    
    print("Generating animation frames...")
    
    # 2. Frame-by-frame rendering loop
    for f in range(num_frames):
        # Always start with a fresh copy of the static canvas
        frame_array = img_array.copy()
        
        left_shift = 0
        right_shift = 0
        
        # Define snappy timing offsets (asymmetric cycle)
        if 5 <= f <= 7:      # Left ear snap outward
            left_shift = -7
        elif 8 <= f <= 9:    # Left ear snap inward bounce
            left_shift = 3
            
        if 14 <= f <= 15:    # Right ear snap inward
            right_shift = -5
        elif 16 <= f <= 17:  # Right ear snap back bounce
            right_shift = 5
        elif 18 <= f <= 19:  # Right ear secondary slight twitch
            right_shift = -3

        # 3. Apply mathematical column-shifting warp (curves the ears outward/inward)
        if left_shift != 0 or right_shift != 0:
            # Only process the top portion of the canvas where ears are located
            for y in range(0, int(height * 0.55)):
                # Decay factor: Tips (y=0) move fully, base (lower down) stays rooted
                factor = (int(height * 0.55) - y) / int(height * 0.55)
                
                # Animate the Left Ear slice zone
                if left_shift != 0:
                    l_amt = int(left_shift * (factor ** 2))
                    x_start, x_end = int(width * 0.20), int(width * 0.50)
                    frame_array[y, x_start:x_end] = np.roll(frame_array[y, x_start:x_end], l_amt, axis=0)
                    
                # Animate the Right Ear slice zone
                if right_shift != 0:
                    r_amt = int(right_shift * (factor ** 2))
                    x_start, x_end = int(width * 0.50), int(width * 0.80)
                    frame_array[y, x_start:x_end] = np.roll(frame_array[y, x_start:x_end], r_amt, axis=0)
        
        # Convert the modified array back to a PIL image frame
        frames.append(Image.fromarray(frame_array))
        
    # 4. Save compiled sequence out to a high-quality GIF
    frames[0].save(
        output_path,
        save_all=True,
        append_images=frames[1:],
        duration=41,  # ~24 frames per second (1000ms / 24 frames)
        loop=0        # Infinite loop configuration
    )
    print(f"Success! Animated output file created at: {os.path.abspath(output_path)}")

if __name__ == "__main__":
    # Example execution configuration:
    # 1. Place your target file next to this script and name it 'input.png'
    # 2. Run the script via your terminal command line
    create_twitch_gif("input.png")
