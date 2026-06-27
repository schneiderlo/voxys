.PHONY: native run-native test wasm serve-wasm clean-native clean-wasm

# Targets
#   native      - Configure/build native binaries into $(BUILD_DIR)
#   run-native  - Build and launch ./$(BUILD_DIR)/bin/voxy_native
#   test        - Run ctest from $(BUILD_DIR)
#   wasm        - Configure/build WASM artifacts into $(WASM_BUILD_DIR)
#   serve-wasm  - Build WASM and serve http://localhost:8080
#   clean-*     - Remove the corresponding build directory

BUILD_DIR        ?= build
WASM_BUILD_DIR   ?= build-wasm
PYTHON           ?= python3
EMSDK            ?= ./third_party/emsdk
EMSDK_ENV        := $(EMSDK)/emsdk_env.sh

native:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -- -j

run-native: native
	./$(BUILD_DIR)/bin/voxy_native

test: native
	ctest --test-dir $(BUILD_DIR) --output-on-failure

wasm:
	@if [ ! -f "$(EMSDK_ENV)" ]; then \
		echo "Emscripten environment not found at $(EMSDK_ENV)"; \
		echo "Set EMSDK=<path-to-emsdk> or install emsdk first."; \
		exit 1; \
	fi
	bash -lc "source '$(EMSDK_ENV)' && emcmake cmake -S . -B $(WASM_BUILD_DIR) -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
	rm -f $(WASM_BUILD_DIR)/bin/voxy_wasm.*
	bash -lc "source '$(EMSDK_ENV)' && cmake --build $(WASM_BUILD_DIR) -- -j"
	cp web/index.html web/loader.js web/style.css $(WASM_BUILD_DIR)/
	cp $(WASM_BUILD_DIR)/bin/voxy_wasm.* $(WASM_BUILD_DIR)/

serve-wasm: wasm
	$(PYTHON) -m http.server 8080 --directory $(WASM_BUILD_DIR)

clean-native:
	rm -rf $(BUILD_DIR)

clean-wasm:
	rm -rf $(WASM_BUILD_DIR)
