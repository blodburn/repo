# Rust TUI: 실시간 편집 및 SQLite 복구 (v0.4 개발)

## 실행

cargo run --release -- /path/to/your-story

- 실제 터미널에서 실행해야 합니다. Mac Terminal은 Command 키를 TUI에 전달하지 않을 수 있어 Ctrl 단축키가 기본입니다. 기존 Swift GUI는 Cmd를 유지합니다.
- 프로젝트 경로를 생략하면 현재 작업 폴더를 프로젝트로 취급합니다.

## 키보드

| 키 | 동작 |
|---|---|
| F1 / F6 | 도움말 / 좌측·중앙·우측 패널 전환 |
| Ctrl+S / Ctrl+Q | 저장 / 저장하고 종료 |
| Ctrl+Z / Ctrl+U | Undo / Redo |
| Ctrl+N / Ctrl+O / Ctrl+T | 새 프로젝트 / 열기 / 작품 제목 |
| Ctrl+Shift+O | 커서 아래 단어를 오브젝트로 등록 |
| Ctrl+G | 현재 선택 오브젝트의 다음 등장으로 이동 |
| Ctrl+Shift+H | 이전 스냅샷이나 삭제 문장 복구 |
| Ctrl+Shift+P | 문법 기호를 제거한 읽기 미리보기 |
| 방향키 / Home / End / PageUp / PageDown | 문장과 커서 이동 |

## 문법

- # 병원 - 밤 : 장면
- @@ 대사 @@ : 대화
- ## 문이 열린다 ## : 상황·행동
- ₩₩ 그날 밤... ₩₩ : 나레이션
- [[김철수]] / [[장소:서울역]] : 오브젝트 명시 등록
- 등록 후 일반 문장의 김철수도 자동 인식합니다.

## 영구 저장

- script/main.txt: UTF-8 원고, 임시 파일에서 원자적으로 교체
- project.sqlite: SQLite WAL, FULL 동기화
- repo_edit_journal: 모든 편집의 시작 위치·삭제·삽입 문자 기록
- repo_snapshots: 128회 수정 후 자동 스냅샷, Ctrl+S와 자동 저장 시도 시 스냅샷
- repo_objects / repo_object_aliases: 오브젝트 ID·이름·별칭을 영구 등록
- Ctrl+Shift+H: 삭제 이력이나 이전 저장본을 찾아 복원
- 입력이 3초간 없으면 TXT 자동저장, 갑작스러운 중단은 저널 재생으로 복원
- 재시작 시 파일과 저널 내용이 다르면 기존 TXT의 충돌 백업을 먼저 생성

## 검증 및 한계

- cargo test --workspace --all-targets
- cargo run -- --encoding-self-test
- cargo run -- --style-self-test
- cargo run -- --self-test
- python3 rust/scripts/tui_pty_smoke.py (Linux PTY 통합 테스트)

Rust TUI는 기존 C++/Swift GUI Release와 별도 실행 프로그램입니다. 이번 작업에서는 GUI 배포를 교체하지 않습니다.
Mac TUI의 Command 키는 일반적으로 터미널에 전달되지 않아 Ctrl 사용이 필요합니다.
Rust PDF 한글 CIDFont 보강과 HWPX 직접 호환성 검사, 과거 GUI DB의 객체 정보 동기화는 미완료 상태입니다.