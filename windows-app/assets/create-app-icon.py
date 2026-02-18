#!/usr/bin/env python3
"""
Create proper 256x256 app icon from favicon
"""
from PIL import Image
import os

def create_app_icon():
    """Create a 256x256 app icon from the favicon"""
    try:
        # Open the favicon
        with Image.open('app-icon.ico') as img:
            print(f"Original favicon size: {img.size}")
            
            # Convert to RGBA if not already
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            # Resize to 256x256 with high quality resampling (base image)
            base256 = img.resize((256, 256), Image.Resampling.LANCZOS)

            # Save a dedicated 256x256 ICO (for debugging/verification)
            base256.save('app-icon-256.ico', format='ICO', sizes=[(256, 256)])
            print("Created app-icon-256.ico (256x256)")

            # Create a proper multi-size ICO using PIL's sizes parameter
            sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
            base256.save('app-icon-multi.ico', format='ICO', sizes=sizes)
            print("Created app-icon-multi.ico (multi-size: " + ", ".join(f"{w}x{h}" for w, h in sizes) + ")")
            
    except Exception as e:
        print(f"Error processing favicon: {e}")
        print("Creating a simple placeholder icon...")
        
        # Create a simple placeholder if the favicon fails
        placeholder = Image.new('RGBA', (256, 256), (70, 130, 180, 255))  # Steel blue
        
        # Add a simple "A" for AIONMECH
        from PIL import ImageDraw, ImageFont
        draw = ImageDraw.Draw(placeholder)
        
        try:
            # Try to use a system font
            font = ImageFont.truetype("arial.ttf", 120)
        except:
            try:
                font = ImageFont.truetype("calibri.ttf", 120)
            except:
                font = ImageFont.load_default()
        
        # Draw "A" centered
        text = "A"
        bbox = draw.textbbox((0, 0), text, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        x = (256 - text_width) // 2
        y = (256 - text_height) // 2
        
        draw.text((x, y), text, fill=(255, 255, 255, 255), font=font)
        
        placeholder.save('app-icon-256.ico', format='ICO')
        print("Created placeholder app-icon-256.ico")

if __name__ == "__main__":
    create_app_icon()