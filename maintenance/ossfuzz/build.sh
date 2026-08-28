#!/usr/bin/env bash
#
# Build the fuzz targets for OSS-Fuzz (and ClusterFuzzLite).
#
# This is the real build logic for the google/oss-fuzz integration, kept here
# so that changes to the source layout, the fuzzer set or the seed corpora do
# not require a pull request against google/oss-fuzz. The build.sh registered
# there is a one line delegation to this script.
#
# OSS-Fuzz supplies the toolchain and the instrumentation, so this script must
# not choose a compiler or add warning flags of its own: a new Clang release
# emitting a new warning would otherwise break the OSS-Fuzz build.
#
# Required environment: CC CXX CFLAGS CXXFLAGS LIB_FUZZING_ENGINE WORK OUT
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT/src/c-lib"

OBJDIR="$WORK/objects"
SEEDDIR="$WORK/seeds"
rm -rf "$OBJDIR" "$SEEDDIR"
mkdir -p "$OBJDIR"

# The only define that the library build requires; everything else that the
# Makefile passes is either a warning flag or an instrumentation flag that
# OSS-Fuzz supplies through $CFLAGS. Quoted includes resolve relative to the
# including file, so no -I is needed.
DEFINES=(-DGS1_LINTER_ERR_STR_EN)

# Mirrors the Makefile's SRCS: every translation unit except the example, the
# test harnesses and the fuzzers themselves.
mapfile -t LIB_SRCS < <(
	{
		find . -maxdepth 1 -name '*.c' \
			! -name 'example.c' \
			! -name 'gs1encoders-test.c' \
			! -name 'gs1encoders-fuzzer-*.c'
		find syntax -maxdepth 1 -name '*.c' \
			! -name 'gs1syntaxdictionary-test.c'
	} | sort
)

LIB_OBJS=()
for src in "${LIB_SRCS[@]}"; do
	rel="${src#./}"
	obj="$OBJDIR/${rel//\//_}.o"
	# shellcheck disable=SC2086  # $CFLAGS must undergo word splitting
	$CC $CFLAGS "${DEFINES[@]}" -c "$src" -o "$obj"
	LIB_OBJS+=("$obj")
done

# Mirrors the Makefile's FUZZER_SEED_SOURCES: quoted string literals from the
# test harness and the parsers make effective starting inputs for every target
# other than syn, whose input is a whole Syntax Dictionary.
SEED_SOURCES=(gs1encoders-test.c dl.c ai.c scandata.c syn.c)

extract_seeds() {
	local dir="$1"
	mkdir -p "$dir"
	awk -v dir="$dir" '
	{
		while (match($0, /"[^"\\]*(\\.[^"\\]*)*"/)) {
			str = substr($0, RSTART + 1, RLENGTH - 2);
			$0 = substr($0, RSTART + RLENGTH);
			gsub(/\\"/, "\"", str);
			gsub(/\\\\/, "\\", str);
			if (length(str) > 0 && !seen[str]++) {
				f = sprintf("%s/seed-%05d", dir, ++n);
				printf "%s", str > f;
				close(f);
			}
		}
	}' "${SEED_SOURCES[@]}"
}

extract_seeds "$SEEDDIR/strings"
mkdir -p "$SEEDDIR/syn"
cp gs1-syntax-dictionary.txt "$SEEDDIR/syn/seed-syntax-dictionary"

# The harnesses copy the input into a char[MAX_DATA+50] buffer and reject
# anything longer, so cap generated inputs at the largest accepted length:
# without this libFuzzer would settle on its 4096 byte default and never
# exercise the upper end of those buffers.
MAX_DATA="$(sed -n 's/^#define[[:space:]]\+MAX_DATA[[:space:]]\+\([0-9]\+\).*/\1/p' enc-private.h)"
MAX_LEN=$((MAX_DATA + 49))

mapfile -t FUZZERS < <(find . -maxdepth 1 -name 'gs1encoders-fuzzer-*.c' -printf '%f\n' | sort)

for src in "${FUZZERS[@]}"; do

	bin="${src%.c}"
	name="${bin#gs1encoders-fuzzer-}"
	obj="$OBJDIR/$bin.o"

	# shellcheck disable=SC2086  # $CFLAGS must undergo word splitting
	$CC $CFLAGS "${DEFINES[@]}" -c "$src" -o "$obj"

	# OSS-Fuzz requires $CXX for the link even for a C project, since the
	# fuzzing engine may depend on the C++ runtime.
	# shellcheck disable=SC2086  # $CXXFLAGS and $LIB_FUZZING_ENGINE likewise
	$CXX $CXXFLAGS "${LIB_OBJS[@]}" "$obj" $LIB_FUZZING_ENGINE -o "$OUT/$bin"

	if [ "$name" = "syn" ]; then
		(cd "$SEEDDIR/syn" && zip -q -r "$OUT/${bin}_seed_corpus.zip" .)
		continue
	fi

	(cd "$SEEDDIR/strings" && zip -q -r "$OUT/${bin}_seed_corpus.zip" .)

	cat >"$OUT/$bin.options" <<-EOF
		[libfuzzer]
		max_len = $MAX_LEN
	EOF

done
