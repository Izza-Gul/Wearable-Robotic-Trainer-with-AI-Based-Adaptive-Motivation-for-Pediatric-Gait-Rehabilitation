#!/usr/bin/env python3
# ============================================
# تحليل الصورتين - يطبع النتيجة كـ JSON على stdout
# نسخة محدثة تستخدم MediaPipe Tasks API الجديد
# (لا تحتاج opencv نهائيًا - تم حذفها بالكامل)
# ============================================

import sys
import os
import json
import math
import urllib.request

import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

# ============================================
# مسار ملف الموديل (يتحمّل تلقائيًا أول مرة فقط)
# ============================================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "pose_landmarker_lite.task")
MODEL_URL = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task"

# ============================================
# مسارات الصورتين - عدّلي هنا لو الأسماء مختلفة
# ============================================
DEFAULT_IMG1 = "/Users/bushra/Desktop/frame1.jpg"
DEFAULT_IMG2 = "/Users/bushra/Desktop/frame2.jpg"


def ensure_model():
    """تحميل ملف الموديل أول مرة فقط (يحتاج إنترنت لمرة واحدة)"""
    if not os.path.exists(MODEL_PATH):
        urllib.request.urlretrieve(MODEL_URL, MODEL_PATH)


def get_detector():
    ensure_model()
    base_options = mp_python.BaseOptions(model_asset_path=MODEL_PATH)
    options = vision.PoseLandmarkerOptions(
        base_options=base_options,
        output_segmentation_masks=False
    )
    return vision.PoseLandmarker.create_from_options(options)


def detect_landmarks(detector, image_path):
    image = mp.Image.create_from_file(image_path)
    result = detector.detect(image)
    if not result.pose_landmarks:
        return None
    return result.pose_landmarks[0]  # أول شخص مكتشف بالصورة


def analyze_movement(landmarks1, landmarks2):
    joints = {
        "Left Shoulder": 11, "Right Shoulder": 12,
        "Left Elbow": 13, "Right Elbow": 14,
        "Left Wrist": 15, "Right Wrist": 16,
        "Left Hip": 23, "Right Hip": 24,
        "Left Knee": 25, "Right Knee": 26,
        "Left Ankle": 27, "Right Ankle": 28
    }

    total_movement = 0
    max_movement = 0
    most_moved = ""
    joint_movements = {}

    for name, idx in joints.items():
        if idx < len(landmarks1) and idx < len(landmarks2):
            x1, y1 = landmarks1[idx].x, landmarks1[idx].y
            x2, y2 = landmarks2[idx].x, landmarks2[idx].y
            distance = math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2)
            joint_movements[name] = distance
            total_movement += distance
            if distance > max_movement:
                max_movement = distance
                most_moved = name

    if total_movement > 1.0:
        level = "Very High Activity"
        status = "moving"
    elif total_movement > 0.5:
        level = "Moderate Activity"
        status = "moving"
    elif total_movement > 0.2:
        level = "Light Activity"
        status = "moving"
    else:
        level = "No Movement"
        status = "not moving"

    upper = sum(joint_movements.get(j, 0) for j in
                ["Left Shoulder", "Right Shoulder", "Left Elbow", "Right Elbow"])
    lower = sum(joint_movements.get(j, 0) for j in
                ["Left Knee", "Right Knee", "Left Ankle", "Right Ankle"])

    region = "Upper Body (Arms & Shoulders)" if upper > lower else "Lower Body (Knees & Ankles)"

    return {
        "status": status,
        "level": level,
        "score": round(total_movement, 3),
        "most_active": most_moved,
        "active_region": region
    }


def analyze_posture(landmarks):
    left_shoulder = landmarks[11].y
    right_shoulder = landmarks[12].y
    shoulder_diff = abs(left_shoulder - right_shoulder)

    shoulder_center = (landmarks[11].y + landmarks[12].y) / 2
    hip_center = (landmarks[23].y + landmarks[24].y) / 2
    back_curve = abs(shoulder_center - hip_center)

    if shoulder_diff < 0.05 and back_curve < 0.1:
        return "Excellent Posture"
    elif shoulder_diff < 0.1 and back_curve < 0.2:
        return "Good Posture"
    else:
        return "Needs Posture Improvement"


def calculate_angles(landmarks):
    angles = {}

    if all(i < len(landmarks) for i in [12, 14, 16]):
        a = [landmarks[12].x, landmarks[12].y]
        b = [landmarks[14].x, landmarks[14].y]
        c = [landmarks[16].x, landmarks[16].y]
        angle = abs(math.degrees(math.atan2(c[1] - b[1], c[0] - b[0]) -
                                  math.atan2(a[1] - b[1], a[0] - b[0])))
        angles["Right Elbow"] = round(angle, 1)

    if all(i < len(landmarks) for i in [24, 26, 28]):
        a = [landmarks[24].x, landmarks[24].y]
        b = [landmarks[26].x, landmarks[26].y]
        c = [landmarks[28].x, landmarks[28].y]
        angle = abs(math.degrees(math.atan2(c[1] - b[1], c[0] - b[0]) -
                                  math.atan2(a[1] - b[1], a[0] - b[0])))
        angles["Right Knee"] = round(angle, 1)

    return angles


def generate_feedback(movement, posture, angles):
    feedback = []

    if movement["status"] == "moving":
        if "Very High" in movement["level"]:
            feedback.append("Excellent! Great movement activity")
        elif "Moderate" in movement["level"]:
            feedback.append("Good! Active movement detected")
        else:
            feedback.append("Light movement detected")
    else:
        feedback.append("Child is resting/still")

    if "Excellent" in posture:
        feedback.append("Excellent body posture")
    elif "Good" in posture:
        feedback.append("Good body posture")
    else:
        feedback.append("Pay attention to body posture")

    if angles.get("Right Elbow", 0) > 90:
        feedback.append("Right arm in good position")
    if angles.get("Right Knee", 0) > 120:
        feedback.append("Right knee extended well")

    return " | ".join(feedback)


def main():
    img1_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IMG1
    img2_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_IMG2

    try:
        detector = get_detector()
    except Exception as e:
        print(json.dumps({"movement": "not moving", "percentage": 0, "error": f"Model load failed: {e}"}))
        return

    try:
        landmarks1 = detect_landmarks(detector, img1_path)
        landmarks2 = detect_landmarks(detector, img2_path)
    except Exception as e:
        print(json.dumps({"movement": "not moving", "percentage": 0, "error": f"Detection failed: {e}"}))
        return

    if not landmarks1 or not landmarks2:
        print(json.dumps({"movement": "not moving", "percentage": 0, "message": "No body detected"}))
        return

    movement = analyze_movement(landmarks1, landmarks2)
    posture = analyze_posture(landmarks1)
    angles = calculate_angles(landmarks1)
    feedback = generate_feedback(movement, posture, angles)

    percentage = min(100, max(0, round(movement["score"] * 10, 1)))

    result = {
        "movement": movement["status"],
        "percentage": percentage,
        "posture": posture,
        "feedback": feedback,
        "most_active": movement["most_active"],
        "level": movement["level"],
        "angles": angles
    }

    # هذا السطر هو المهم - يطبع النتيجة عشان نود ريد يستقبلها كـ msg.payload
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()