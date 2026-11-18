"""Describe generated artifacts without overwriting a team's existing task notes."""
from datetime import datetime
from pathlib import Path
from uuid import uuid4


def task_id(name):
    return datetime.now().astimezone().strftime('%Y%m%d-%H%M%S%f') + '-' + name + '-' + uuid4().hex[:8]


def describe_task(directory, title, purpose, command, artifacts):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    readme = directory / "README.md"
    text = (f"# {title}\n\n- 목적: {purpose}\n"
            f"- 생성 시각: {datetime.now().astimezone().isoformat(timespec='seconds')}\n"
            f"- 생성 도구/조건: {command}\n\n## 결과 파일과 사용법\n\n"
            + "\n".join(f"- {item}" for item in artifacts)
            + "\n\n## 결과 및 보관\n\n"
            "생성 시점의 설명입니다. 성공 여부는 실행 로그와 보고서를 확인하세요. "
            "원본 CSV/manifest는 보고서와 함께 보관하세요. "
            "이 폴더는 Git 제외 대상일 수 있으므로 공유 시 README를 함께 전달하세요.\n")
    try:
        with readme.open("x", encoding="utf-8") as stream:
            stream.write(text)
    except FileExistsError:
        pass
