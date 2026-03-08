#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

AUTH='Authorization: Bearer dev-token-12345'
SERVER='http://127.0.0.1:8080'
RESULT='/tmp/realistic_perf_results_v3.txt'
: > "$RESULT"

start_server() {
  pkill -f 'build/minis3_server' >/dev/null 2>&1 || true
  ./build/minis3_server configs/server.local.yaml > /tmp/minis3.realistic.log 2>&1 &
  echo $! > /tmp/minis3_realistic.pid
  for _ in $(seq 1 120); do
    code=$(curl -s -o /dev/null -w '%{http_code}' "$SERVER/healthz" || true)
    if [ "$code" = "200" ]; then
      return 0
    fi
    sleep 0.25
  done
  return 1
}

stop_server() {
  if [ -f /tmp/minis3_realistic.pid ]; then
    kill "$(cat /tmp/minis3_realistic.pid)" >/dev/null 2>&1 || true
  fi
  pkill -f 'build/minis3_server' >/dev/null 2>&1 || true
}

calc_avg() {
  local file="$1"
  awk '$1==200{sum+=$2;n++} END{if(n==0){print "N/A"} else {printf "%.6f", sum/n}}' "$file"
}

calc_lat() {
  local file="$1"
  local p="$2"
  awk '$1==200{print $2}' "$file" | sort -n | awk -v p="$p" '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*p)+1; printf "%.6f", a[idx]}}'
}

# W1: PUT 4KB random
start_server
W1_DIR=$(mktemp -d)
W1_BUCKET=realv3_put4k_$(date +%s)
W1_ITER=200
W1_CONC=10
seq 1 $W1_ITER | while read -r i; do head -c 4096 /dev/urandom > "$W1_DIR/p_$i.bin"; done
curl -s -X PUT "$SERVER/buckets/$W1_BUCKET" -H "$AUTH" >/dev/null || true
s=$(date +%s.%N)
seq 1 $W1_ITER | xargs -P $W1_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" -X PUT "$0/buckets/$1/objects/k_{}" -H "$2" -H "Content-Type: application/octet-stream" --data-binary @"$3/p_{}.bin"' "$SERVER" "$W1_BUCKET" "$AUTH" "$W1_DIR" > /tmp/w1v3.txt
e=$(date +%s.%N)
W1_DUR=$(echo "$e - $s" | bc)
W1_OK=$(grep -c '^200 ' /tmp/w1v3.txt || true)
W1_OPS=$(echo "scale=2; $W1_OK / $W1_DUR" | bc)
W1_MBPS=$(echo "scale=2; ($W1_OK * 4096) / $W1_DUR / 1024 / 1024" | bc)
W1_AVG=$(calc_avg /tmp/w1v3.txt)
W1_P50=$(calc_lat /tmp/w1v3.txt 0.50)
W1_P95=$(calc_lat /tmp/w1v3.txt 0.95)
W1_P99=$(calc_lat /tmp/w1v3.txt 0.99)
printf 'W1 PUT_4K_RANDOM bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W1_BUCKET" "$W1_OK" "$W1_ITER" "$W1_DUR" "$W1_OPS" "$W1_MBPS" "$W1_AVG" "$W1_P50" "$W1_P95" "$W1_P99" | tee -a "$RESULT"
stop_server

# W2: PUT 1MB random
start_server
W2_DIR=$(mktemp -d)
W2_BUCKET=realv3_put1m_$(date +%s)
W2_ITER=40
W2_CONC=4
seq 1 $W2_ITER | while read -r i; do head -c 1048576 /dev/urandom > "$W2_DIR/p_$i.bin"; done
curl -s -X PUT "$SERVER/buckets/$W2_BUCKET" -H "$AUTH" >/dev/null || true
s=$(date +%s.%N)
seq 1 $W2_ITER | xargs -P $W2_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" -X PUT "$0/buckets/$1/objects/k_{}" -H "$2" -H "Content-Type: application/octet-stream" --data-binary @"$3/p_{}.bin"' "$SERVER" "$W2_BUCKET" "$AUTH" "$W2_DIR" > /tmp/w2v3.txt
e=$(date +%s.%N)
W2_DUR=$(echo "$e - $s" | bc)
W2_OK=$(grep -c '^200 ' /tmp/w2v3.txt || true)
W2_OPS=$(echo "scale=2; $W2_OK / $W2_DUR" | bc)
W2_MBPS=$(echo "scale=2; ($W2_OK * 1048576) / $W2_DUR / 1024 / 1024" | bc)
W2_AVG=$(calc_avg /tmp/w2v3.txt)
W2_P50=$(calc_lat /tmp/w2v3.txt 0.50)
W2_P95=$(calc_lat /tmp/w2v3.txt 0.95)
W2_P99=$(calc_lat /tmp/w2v3.txt 0.99)
printf 'W2 PUT_1M_RANDOM bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W2_BUCKET" "$W2_OK" "$W2_ITER" "$W2_DUR" "$W2_OPS" "$W2_MBPS" "$W2_AVG" "$W2_P50" "$W2_P95" "$W2_P99" | tee -a "$RESULT"
stop_server

# W3: GET 4KB random key (from W1)
start_server
W3_ITER=2000
W3_CONC=20
seq 1 $W3_ITER | awk -v n=$W1_ITER 'BEGIN{srand()} {print int(1+rand()*n)}' > /tmp/w3_pick_v3.txt
s=$(date +%s.%N)
cat /tmp/w3_pick_v3.txt | xargs -P $W3_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "$0/buckets/$1/objects/k_{}" -H "$2"' "$SERVER" "$W1_BUCKET" "$AUTH" > /tmp/w3v3.txt
e=$(date +%s.%N)
W3_DUR=$(echo "$e - $s" | bc)
W3_OK=$(grep -c '^200 ' /tmp/w3v3.txt || true)
W3_OPS=$(echo "scale=2; $W3_OK / $W3_DUR" | bc)
W3_MBPS=$(echo "scale=2; ($W3_OK * 4096) / $W3_DUR / 1024 / 1024" | bc)
W3_AVG=$(calc_avg /tmp/w3v3.txt)
W3_P50=$(calc_lat /tmp/w3v3.txt 0.50)
W3_P95=$(calc_lat /tmp/w3v3.txt 0.95)
W3_P99=$(calc_lat /tmp/w3v3.txt 0.99)
printf 'W3 GET_4K_RANDOMKEY bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W1_BUCKET" "$W3_OK" "$W3_ITER" "$W3_DUR" "$W3_OPS" "$W3_MBPS" "$W3_AVG" "$W3_P50" "$W3_P95" "$W3_P99" | tee -a "$RESULT"
stop_server

# W4: GET 1MB random key (from W2)
start_server
W4_ITER=500
W4_CONC=10
seq 1 $W4_ITER | awk -v n=$W2_ITER 'BEGIN{srand()} {print int(1+rand()*n)}' > /tmp/w4_pick_v3.txt
s=$(date +%s.%N)
cat /tmp/w4_pick_v3.txt | xargs -P $W4_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "$0/buckets/$1/objects/k_{}" -H "$2"' "$SERVER" "$W2_BUCKET" "$AUTH" > /tmp/w4v3.txt
e=$(date +%s.%N)
W4_DUR=$(echo "$e - $s" | bc)
W4_OK=$(grep -c '^200 ' /tmp/w4v3.txt || true)
W4_OPS=$(echo "scale=2; $W4_OK / $W4_DUR" | bc)
W4_MBPS=$(echo "scale=2; ($W4_OK * 1048576) / $W4_DUR / 1024 / 1024" | bc)
W4_AVG=$(calc_avg /tmp/w4v3.txt)
W4_P50=$(calc_lat /tmp/w4v3.txt 0.50)
W4_P95=$(calc_lat /tmp/w4v3.txt 0.95)
W4_P99=$(calc_lat /tmp/w4v3.txt 0.99)
printf 'W4 GET_1M_RANDOMKEY bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W2_BUCKET" "$W4_OK" "$W4_ITER" "$W4_DUR" "$W4_OPS" "$W4_MBPS" "$W4_AVG" "$W4_P50" "$W4_P95" "$W4_P99" | tee -a "$RESULT"
stop_server

# W5: MIX 70/30 (GET/PUT), 4KB
start_server
W5_BUCKET=realv3_mix_$(date +%s)
W5_PRELOAD=200
W5_ITER=800
W5_CONC=12
W5_DIR=$(mktemp -d)
seq 1 $W5_PRELOAD | while read -r i; do head -c 4096 /dev/urandom > "$W5_DIR/pre_$i.bin"; done
curl -s -X PUT "$SERVER/buckets/$W5_BUCKET" -H "$AUTH" >/dev/null || true
seq 1 $W5_PRELOAD | xargs -P 12 -I {} sh -c 'curl -s -o /dev/null -X PUT "$0/buckets/$1/objects/base_{}" -H "$2" -H "Content-Type: application/octet-stream" --data-binary @"$3/pre_{}.bin"' "$SERVER" "$W5_BUCKET" "$AUTH" "$W5_DIR"
: > /tmp/w5_ops_v3.txt
for i in $(seq 1 $W5_ITER); do
  if [ $((RANDOM % 10)) -lt 7 ]; then
    k=$((1 + RANDOM % W5_PRELOAD))
    echo "G base_$k" >> /tmp/w5_ops_v3.txt
  else
    head -c 4096 /dev/urandom > "$W5_DIR/put_$i.bin"
    echo "P put_$i $W5_DIR/put_$i.bin" >> /tmp/w5_ops_v3.txt
  fi
done
s=$(date +%s.%N)
cat /tmp/w5_ops_v3.txt | xargs -P $W5_CONC -I {} bash -lc 'set -- {}; if [ "$1" = "G" ]; then curl -s -o /dev/null -w "%{http_code} %{time_total} G\n" "$0/buckets/$1/objects/$2" -H "$3"; else curl -s -o /dev/null -w "%{http_code} %{time_total} P\n" -X PUT "$0/buckets/$1/objects/$2" -H "$3" -H "Content-Type: application/octet-stream" --data-binary @"$4"; fi' "$SERVER" "$W5_BUCKET" "$AUTH" > /tmp/w5v3.txt
e=$(date +%s.%N)
W5_DUR=$(echo "$e - $s" | bc)
W5_OK=$(grep -c '^200 ' /tmp/w5v3.txt || true)
W5_OPS=$(echo "scale=2; $W5_OK / $W5_DUR" | bc)
W5_G_OK=$(awk '$1==200 && $3=="G"{c++} END{print c+0}' /tmp/w5v3.txt)
W5_P_OK=$(awk '$1==200 && $3=="P"{c++} END{print c+0}' /tmp/w5v3.txt)
W5_MBPS=$(echo "scale=2; (($W5_G_OK + $W5_P_OK) * 4096) / $W5_DUR / 1024 / 1024" | bc)
W5_AVG=$(awk '$1==200{sum+=$2;n++} END{if(n==0){print "N/A"} else {printf "%.6f", sum/n}}' /tmp/w5v3.txt)
W5_P50=$(awk '$1==200{print $2}' /tmp/w5v3.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.50)+1; printf "%.6f", a[idx]}}')
W5_P95=$(awk '$1==200{print $2}' /tmp/w5v3.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.95)+1; printf "%.6f", a[idx]}}')
W5_P99=$(awk '$1==200{print $2}' /tmp/w5v3.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.99)+1; printf "%.6f", a[idx]}}')
printf 'W5 MIX_70G30P_4K bucket=%s success=%s/%s (G_ok=%s,P_ok=%s) duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W5_BUCKET" "$W5_OK" "$W5_ITER" "$W5_G_OK" "$W5_P_OK" "$W5_DUR" "$W5_OPS" "$W5_MBPS" "$W5_AVG" "$W5_P50" "$W5_P95" "$W5_P99" | tee -a "$RESULT"
stop_server

echo "RESULT_FILE=$RESULT"