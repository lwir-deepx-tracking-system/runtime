#!/usr/bin/env python3
"""ByteTrack 시나리오 목업 데이터 생성기.

실행하면 이 폴더 아래에 시나리오별 detections.csv / frames.csv를 다시 만든다.
    python3 tests/tracking/data/scenarios/generate.py
좌표는 640x480 LWIR 영상 기준, (x, y)는 bbox 좌상단이다.
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = ["frame_id", "x", "y", "width", "height", "confidence", "class_id"]


def write(name, num_frames, rows, note):
    out = os.path.join(HERE, name)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "detections.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        for r in sorted(rows, key=lambda r: r[0]):
            w.writerow([r[0]] + [f"{v:.1f}" for v in r[1:5]] + [f"{r[5]:.2f}", r[6]])
    with open(os.path.join(out, "frames.csv"), "w", newline="") as f:
        f.write("frame_id\n")
        for i in range(1, num_frames + 1):
            f.write(f"{i}\n")
    with open(os.path.join(out, "README.txt"), "w") as f:
        f.write(note.strip() + "\n")


# 1. 교차: A는 왼쪽→오른쪽, B는 오른쪽→왼쪽. 15~16프레임 부근에서 겹친다.
#    기대: 끝까지 A는 오른쪽, B는 왼쪽에 같은 ID로 도착 (ID 뒤바뀜 없음)
rows = []
for f in range(1, 31):
    rows.append([f, 100 + 10 * (f - 1), 200, 40, 80, 0.90, 0])  # A
    rows.append([f, 400 - 10 * (f - 1), 215, 40, 80, 0.85, 0])  # B
write("crossing", 30, rows, """
교차: 물체 A(x=100에서 오른쪽으로 10px/프레임)와 B(x=400에서 왼쪽으로 10px/프레임)가
15~16프레임에서 겹쳤다가 지나간다. 마지막 프레임에서 A 위치(오른쪽)의 ID가
1프레임의 A ID와 같아야 한다.
""")

# 2. 오검출: 정지 물체 1개 + 10번 프레임에만 나타나는 높은 점수 검출.
#    20번 프레임부터 진짜 새 물체 등장.
#    기대: 오검출 위치에는 Track이 출력되지 않음, 진짜 새 물체는 다음 프레임부터 출력
rows = []
for f in range(1, 31):
    rows.append([f, 300, 200, 40, 80, 0.90, 0])
rows.append([10, 500, 50, 30, 30, 0.95, 0])          # 한 프레임짜리 오검출
for f in range(20, 31):
    rows.append([f, 80, 350, 40, 60, 0.88, 0])        # 진짜 새 물체
write("false_positive", 30, rows, """
오검출: (300,200) 정지 물체가 계속 보이고, 10번 프레임에만 (500,50)에 점수 0.95 검출이
나타난다. 20번 프레임부터 (80,350)에 진짜 새 물체가 계속 보인다.
오검출 위치에는 어떤 프레임에서도 Track이 출력되면 안 되고, 새 물체는 21번 프레임부터
출력되어야 한다.
""")

# 3. track_buffer: 두 정지 물체가 5프레임 뒤 사라진다.
#    A는 40프레임 뒤(46번), B는 20프레임 뒤(26번) 같은 자리에 다시 나타난다.
#    기대(track_buffer=30): B는 원래 ID 복구, A는 새 ID
rows = []
for f in list(range(1, 6)) + list(range(46, 51)):
    rows.append([f, 100, 200, 40, 80, 0.90, 0])       # A
for f in list(range(1, 6)) + list(range(26, 51)):
    rows.append([f, 450, 200, 40, 80, 0.90, 0])       # B
write("track_buffer", 50, rows, """
track_buffer: A(100,200)와 B(450,200)가 1~5프레임에 보이고 사라진다.
B는 26번 프레임(20프레임 공백)에, A는 46번 프레임(40프레임 공백)에 같은 자리로 돌아온다.
track_buffer=30 기준으로 B는 원래 ID, A는 새 ID여야 한다.
""")
# 4. 낮은 점수: 한 물체가 오른쪽으로 이동하다 11~15프레임 동안 가려져 점수가 0.3으로 떨어진다.
#    기대: 2차 매칭(low detection)으로 가려진 구간에도 같은 ID로 계속 출력
rows = []
for f in range(1, 26):
    score = 0.30 if 11 <= f <= 15 else 0.90
    rows.append([f, 100 + 5 * (f - 1), 200, 40, 80, score, 0])
write("low_score", 25, rows, """
낮은 점수: 물체가 x=100에서 오른쪽으로 5px/프레임 이동한다. 11~15프레임은 가려져
점수가 0.30(track_thresh 0.5 미만)으로 떨어진다. ByteTrack의 2차 매칭으로
1~25프레임 모두 같은 ID가 출력되어야 한다.
""")
print("generated:", ", ".join(["crossing", "false_positive", "track_buffer", "low_score"]))
