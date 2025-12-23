.PHONY: wheel develop test format clean

wheel:
	uv build --wheel

develop: wheel
	uv pip install -e . --force-reinstall
	@echo "Copying .pyi stub file..."
	@cp _build/cp*/portaudio_ext.pyi src/portaudio/ 2>/dev/null || true

test: develop
	uv run pytest tests/ --timeout=10

format:
	clang-format -i src/portaudio_ext.cpp
	uv run ruff format src/portaudio examples/ 

clean:
	rm -rf _build dist *.egg-info _deps
