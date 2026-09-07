import cv2
import time
import os

# مسار حفظ الصور
desktop_path = os.path.expanduser("~/Desktop")
frame1_path = os.path.join(desktop_path, "frame1.jpg")
frame2_path = os.path.join(desktop_path, "frame2.jpg")

# فتح كاميرا اللابتوب
cap = cv2.VideoCapture(0)
if not cap.isOpened():
    print("Failed to open camera")
    exit()

# 🔹 انتظر ثانيتين لتعديل الإضاءة
time.sleep(2)

# التقاط الصورة الأولى
ret, frame1 = cap.read()
if ret:
    cv2.imwrite(frame1_path, frame1)
    print(f"Captured first frame: {frame1_path}")
else:
    print("Failed to capture first frame")

# انتظر ثانية قبل الصورة الثانية
time.sleep(2)

# التقاط الصورة الثانية
ret, frame2 = cap.read()
if ret:
    cv2.imwrite(frame2_path, frame2)
    print(f"Captured second frame: {frame2_path}")
else:
    print("Failed to capture second frame")

cap.release()