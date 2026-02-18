#!/usr/bin/env python3
"""
Simple script to create a tray icon for the MR-1 Pendant Controller
Requires: pip install pillow
"""

from PIL import Image, ImageDraw
import os

def create_tray_icon():
    # Create a 32x32 image with transparent background
    size = 32
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Draw a simple pendant icon - circle with cross
    center = size // 2
    radius = 12
    
    # Outer circle (pendant body)
    draw.ellipse([center-radius, center-radius, center+radius, center+radius], 
                 outline=(100, 150, 255, 255), width=3)
    
    # Cross for direction control
    line_len = 8
    draw.line([center-line_len, center, center+line_len, center], 
              fill=(100, 150, 255, 255), width=2)
    draw.line([center, center-line_len, center, center+line_len], 
              fill=(100, 150, 255, 255), width=2)
    
    # Center dot
    draw.ellipse([center-2, center-2, center+2, center+2], 
                 fill=(100, 150, 255, 255))
    
    # Save as PNG
    img.save('tray-icon.png')
    print("Created tray-icon.png")

if __name__ == "__main__":
    create_tray_icon()
