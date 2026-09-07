import sys
import json
import ast
import math
from PIL import Image, ImageDraw

# -------------------------------------------------
# SAFE INPUT
# -------------------------------------------------
def default_data():
    return {
        "movement": "",
        "percentage": 0,
        "posture": "",
        "most_active": "",
        "level": "",
        "angles": {},
        "feedback": ""
    }


def read_input():
    raw = sys.argv[1] if len(sys.argv) > 1 else ""

    if raw in ["", "[object Object]", None]:
        return default_data()

    try:
        return json.loads(raw)
    except:
        try:
            return ast.literal_eval(raw)
        except:
            return default_data()

# -------------------------------------------------
# SMART SCORE
# -------------------------------------------------
def compute_score(data):
    score = float(data.get("percentage", 0))
    posture = data.get("posture", "").lower()

    if "need" in posture or "improv" in posture:
        score -= 20
    if "good" in posture or "excellent" in posture:
        score += 10

    return max(0, min(100, score))

# -------------------------------------------------
# MOOD
# -------------------------------------------------
def get_mood(score):
    if score >= 75:
        return "good"
    elif score >= 40:
        return "neutral"
    else:
        return "bad"

# -------------------------------------------------
# SAFE FACE (NO ARC USED)
# -------------------------------------------------
def draw_face(draw, cx, cy, mood, pulse):
    r = int(120 * pulse)

    skin = (255, 224, 189)

    # shadow
    draw.ellipse([cx-r-5, cy-r+5, cx+r+5, cy+r+5], fill=(0, 0, 0, 40))

    # head
    draw.ellipse([cx-r, cy-r, cx+r, cy+r], fill=skin)

    # eyes
    eye_y = cy - 30
    eye_x = 45

    for dx in (-eye_x, eye_x):
        draw.ellipse([cx+dx-18, eye_y-18, cx+dx+18, eye_y+18], fill="white")
        draw.ellipse([cx+dx-7, eye_y-7, cx+dx+7, eye_y+7], fill="black")

    # eyebrows
    if mood == "good":
        brow = -10
    elif mood == "bad":
        brow = 10
    else:
        brow = 0

    draw.line([cx-70, eye_y-40+brow, cx-20, eye_y-40], fill="black", width=6)
    draw.line([cx+20, eye_y-40, cx+70, eye_y-40+brow], fill="black", width=6)

    # mouth (SAFE: lines only, no arc)
    if mood == "good":
        draw.line([cx-50, cy+40, cx, cy+60, cx+50, cy+40], fill="black", width=6)

    elif mood == "bad":
        draw.line([cx-50, cy+60, cx, cy+40, cx+50, cy+60], fill="black", width=6)

    else:
        draw.line([cx-40, cy+50, cx+40, cy+50], fill="black", width=5)

# -------------------------------------------------
# BACKGROUND
# -------------------------------------------------
def make_bg(score):
    if score >= 75:
        return (200, 255, 210), (240, 255, 245)
    elif score >= 40:
        return (220, 220, 255), (245, 245, 255)
    else:
        return (255, 210, 210), (255, 240, 240)

# -------------------------------------------------
# GENERATE
# -------------------------------------------------
def generate(data):
    score = compute_score(data)
    mood = get_mood(score)

    top, bottom = make_bg(score)

    frames = []

    for i in range(8):
        img = Image.new("RGB", (600, 600), top)
        draw = ImageDraw.Draw(img)

        # gradient
        for y in range(600):
            t = y / 600
            r = int(top[0]*(1-t) + bottom[0]*t)
            g = int(top[1]*(1-t) + bottom[1]*t)
            b = int(top[2]*(1-t) + bottom[2]*t)
            draw.line([(0, y), (600, y)], fill=(r, g, b))

        # pulse animation
        pulse = 1.0 + 0.03 * math.sin(i)

        draw_face(draw, 300, 300, mood, pulse)

        frames.append(img)

    output_path = "/Users/bushra/Desktop/feedback_output.gif"

    frames[0].save(
        output_path,
        save_all=True,
        append_images=frames[1:],
        duration=120,
        loop=0
    )

    return output_path

# -------------------------------------------------
# MAIN
# -------------------------------------------------
def main():
    data = read_input()

    path = generate(data)

    print(json.dumps({
        "image_path": path,
        "status": "ok",
        "score": compute_score(data),
        "mood": get_mood(compute_score(data))
    }))


if __name__ == "__main__":
    main()