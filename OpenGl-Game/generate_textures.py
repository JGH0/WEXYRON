from PIL import Image, ImageDraw
import os

# Create textures directory
tex_dir = "assets/textures"
os.makedirs(tex_dir, exist_ok=True)

def create_texture(name, color1, color2, pattern="checker"):
    """Create a 64x64 texture with pattern"""
    img = Image.new('RGB', (64, 64), color1)
    draw = ImageDraw.Draw(img)
    
    if pattern == "checker":
        # Draw checkerboard pattern
        for y in range(0, 64, 8):
            for x in range(0, 64, 8):
                if (x//8 + y//8) % 2 == 0:
                    draw.rectangle([x, y, x+8, y+8], fill=color2)
    elif pattern == "brick":
        # Brick pattern
        for y in range(0, 64, 16):
            offset = 8 if (y//16) % 2 == 0 else 0
            for x in range(-8, 64, 16):
                draw.rectangle([x+offset, y, x+offset+15, y+15], fill=color2, outline=color1)
    elif pattern == "noise":
        # Noise pattern
        import random
        random.seed(42)
        for y in range(0, 64, 4):
            for x in range(0, 64, 4):
                if random.random() > 0.5:
                    draw.rectangle([x, y, x+4, y+4], fill=color2)
    elif pattern == "wood":
        # Wood grain
        for y in range(0, 64, 8):
            draw.rectangle([0, y, 64, y+6], fill=color2)
    
    img.save(f"{tex_dir}/{name}.png")
    print(f"Created {name}.png")

# Create various textures
create_texture("grass", (34, 139, 34), (50, 205, 50), "noise")      # 0: Grass
create_texture("stone", (105, 105, 105), (128, 128, 128), "checker") # 1: Stone
create_texture("brick", (139, 69, 19), (178, 34, 34), "brick")     # 2: Brick
create_texture("metal", (70, 70, 80), (100, 100, 110), "checker")   # 3: Metal
create_texture("wood", (101, 67, 33), (139, 90, 43), "wood")       # 4: Wood
create_texture("sand", (194, 178, 128), (210, 190, 140), "noise")   # 5: Sand
create_texture("ice", (200, 220, 255), (220, 240, 255), "checker")  # 6: Ice
create_texture("dirt", (101, 67, 33), (120, 80, 50), "noise")       # 7: Dirt

print("All textures created!")
