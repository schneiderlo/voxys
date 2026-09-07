.PHONY: native run-native test wasm package-wasm serve-wasm clean-native clean-wasm

# Targets
#   native      - Configure/build native binaries into $(BUILD_DIR)
#   run-native  - Build and launch ./$(BUILD_DIR)/bin/voxy_native
#   test        - Run ctest from $(BUILD_DIR)
#   wasm        - Configure/build WASM artifacts into $(WASM_BUILD_DIR)
#   package-wasm - Stage existing WASM artifacts and all browser assets
#   serve-wasm  - Build WASM and serve http://localhost:8080
#   clean-*     - Remove the corresponding build directory

BUILD_DIR        ?= build
WASM_BUILD_DIR   ?= build-wasm
WASM_PACKAGE_DIR ?= $(WASM_BUILD_DIR)
JOBS             ?= 4
PYTHON           ?= python3
EMSDK            ?= ./third_party/emsdk
EMSDK_ENV        := $(EMSDK)/emsdk_env.sh

native:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

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
	bash -lc "source '$(EMSDK_ENV)' && emcmake cmake -S . -B '$(WASM_BUILD_DIR)' -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DVOXY_BUILD_NATIVE=OFF -DVOXY_BUILD_WASM=ON"
	bash -lc "source '$(EMSDK_ENV)' && cmake --build '$(WASM_BUILD_DIR)' --target voxy_wasm --parallel $(JOBS)"
	$(MAKE) --no-print-directory package-wasm

package-wasm:
	@test -s "$(WASM_BUILD_DIR)/bin/voxy_wasm.js" && \
		test -s "$(WASM_BUILD_DIR)/bin/voxy_wasm.wasm" && \
		test -s "$(WASM_BUILD_DIR)/bin/voxy_wasm.data" || \
		{ echo "Build the WASM target before packaging it."; exit 1; }
	mkdir -p "$(WASM_PACKAGE_DIR)/shaders"
	cp -R web/. "$(WASM_PACKAGE_DIR)/"
	# The standalone shader parity page fetches these over HTTP.
	cp shaders/*.wgsl "$(WASM_PACKAGE_DIR)/shaders/"
	cp "$(WASM_BUILD_DIR)/bin/voxy_wasm.js" \
		"$(WASM_BUILD_DIR)/bin/voxy_wasm.wasm" \
		"$(WASM_BUILD_DIR)/bin/voxy_wasm.data" "$(WASM_PACKAGE_DIR)/"

serve-wasm: wasm
	$(PYTHON) -m http.server 8080 --directory "$(WASM_PACKAGE_DIR)"

clean-native:
	rm -rf $(BUILD_DIR)

clean-wasm:
	rm -rf $(WASM_BUILD_DIR)
