#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

source /tmp/realistic_bench_utils.sh

if [ ! -f /tmp/realistic_perf_results_v3.txt ]; then
  echo "missing /tmp/realistic_perf_results_v3.txt"
  exit 1
fi

W1_BUCKET=$(awk '/W1 PUT_4K_RANDOM/{for(i=1;i<=NF;i++) if($i ~ /^bucket=/){sub("bucket=","",$i); print $i}}' /tmp/realistic_perf_results_v3.txt | tail -n1)
W2_BUCKET=$(awk '/W2 PUT_1M_RANDOM/{for(i=1;i<=NF;i++) if($i ~ /^bucket=/){sub("bucket=","",$i); print $i}}' /tmp/realistic_perf_results_v3.txt | tail -n1)

if [ -z "$W1_BUCKET" ] || [ -z "$W2_BUCKET" ]; then
  echo "missing buckets from v3 results"
  exit 1
fi

# W3 low-pressure GET 4KB
start_server
W3_ITER=800
W3_CONC=5
seq 1 $W3_ITER | awk -v n=200 'BEGIN{srand()} {print int(1+rand()*n)}' > /tmp/w3v3_low_pick.txt
s=$(date +%s.%N)
cat /tmp/w3v3_low_pick.txt | xargs -P $W3_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "$0/buckets/$1/objects/k_{}" -H "$2"' "$SERVER" "$W1_BUCKET" "$AUTH" > /tmp/w3v3_low.txt || true
e=$(date +%s.%N)
W3_DUR=$(echo "$e - $s" | bc)
W3_OK=$(grep -c '^200 ' /tmp/w3v3_low.txt || true)
W3_OPS=$(echo "scale=2; $W3_OK / $W3_DUR" | bc)
W3_MBPS=$(echo "scale=2; ($W3_OK * 4096) / $W3_DUR / 1024 / 1024" | bc)
W3_AVG=$(calc_avg /tmp/w3v3_low.txt)
W3_P50=$(calc_lat /tmp/w3v3_low.txt 0.50)
W3_P95=$(calc_lat /tmp/w3v3_low.txt 0.95)
W3_P99=$(calc_lat /tmp/w3v3_low.txt 0.99)
printf 'W3 GET_4K_RANDOMKEY_LOW bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W1_BUCKET" "$W3_OK" "$W3_ITER" "$W3_DUR" "$W3_OPS" "$W3_MBPS" "$W3_AVG" "$W3_P50" "$W3_P95" "$W3_P99" | tee -a /tmp/realistic_perf_results_v3.txt
stop_server

# W4 low-pressure GET 1MB
start_server
W4_ITER=200
W4_CONC=4
seq 1 $W4_ITER | awk -v n=40 'BEGIN{srand()} {print int(1+rand()*n)}' > /tmp/w4v3_low_pick.txt
s=$(date +%s.%N)
cat /tmp/w4v3_low_pick.txt | xargs -P $W4_CONC -I {} sh -c 'curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "$0/buckets/$1/objects/k_{}" -H "$2"' "$SERVER" "$W2_BUCKET" "$AUTH" > /tmp/w4v3_low.txt || true
e=$(date +%s.%N)
W4_DUR=$(echo "$e - $s" | bc)
W4_OK=$(grep -c '^200 ' /tmp/w4v3_low.txt || true)
W4_OPS=$(echo "scale=2; $W4_OK / $W4_DUR" | bc)
W4_MBPS=$(echo "scale=2; ($W4_OK * 1048576) / $W4_DUR / 1024 / 1024" | bc)
W4_AVG=$(calc_avg /tmp/w4v3_low.txt)
W4_P50=$(calc_lat /tmp/w4v3_low.txt 0.50)
W4_P95=$(calc_lat /tmp/w4v3_low.txt 0.95)
W4_P99=$(calc_lat /tmp/w4v3_low.txt 0.99)
printf 'W4 GET_1M_RANDOMKEY_LOW bucket=%s success=%s/%s duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W2_BUCKET" "$W4_OK" "$W4_ITER" "$W4_DUR" "$W4_OPS" "$W4_MBPS" "$W4_AVG" "$W4_P50" "$W4_P95" "$W4_P99" | tee -a /tmp/realistic_perf_results_v3.txt
stop_server

# W5 low-pressure MIX 70/30 4KB
start_server
W5_BUCKET=realv3_mixlow_$(date +%s)
W5_PRELOAD=120
W5_ITER=300
W5_CONC=6
W5_DIR=$(mktemp -d)
seq 1 $W5_PRELOAD | while read -r i; do head -c 4096 /dev/urandom > "$W5_DIR/pre_$i.bin"; done
curl -s -X PUT "$SERVER/buckets/$W5_BUCKET" -H "$AUTH" >/dev/null || true
seq 1 $W5_PRELOAD | xargs -P 6 -I {} sh -c 'curl -s -o /dev/null -X PUT "$0/buckets/$1/objects/base_{}" -H "$2" -H "Content-Type: application/octet-stream" --data-binary @"$3/pre_{}.bin"' "$SERVER" "$W5_BUCKET" "$AUTH" "$W5_DIR"
: > /tmp/w5v3_low_ops.txt
for i in $(seq 1 $W5_ITER); do
  if [ $((RANDOM % 10)) -lt 7 ]; then
    k=$((1 + RANDOM % W5_PRELOAD))
    echo "G base_$k" >> /tmp/w5v3_low_ops.txt
  else
    head -c 4096 /dev/urandom > "$W5_DIR/put_$i.bin"
    echo "P put_$i $W5_DIR/put_$i.bin" >> /tmp/w5v3_low_ops.txt
  fi
done
s=$(date +%s.%N)
cat /tmp/w5v3_low_ops.txt | xargs -P $W5_CONC -I {} bash -lc 'set -- {}; if [ "$1" = "G" ]; then curl -s -o /dev/null -w "%{http_code} %{time_total} G\n" "$0/buckets/$1/objects/$2" -H "$3"; else curl -s -o /dev/null -w "%{http_code} %{time_total} P\n" -X PUT "$0/buckets/$1/objects/$2" -H "$3" -H "Content-Type: application/octet-stream" --data-binary @"$4"; fi' "$SERVER" "$W5_BUCKET" "$AUTH" > /tmp/w5v3_low.txt || true
e=$(date +%s.%N)
W5_DUR=$(echo "$e - $s" | bc)
W5_OK=$(grep -c '^200 ' /tmp/w5v3_low.txt || true)
W5_G_OK=$(awk '$1==200 && $3=="G"{c++} END{print c+0}' /tmp/w5v3_low.txt)
W5_P_OK=$(awk '$1==200 && $3=="P"{c++} END{print c+0}' /tmp/w5v3_low.txt)
W5_OPS=$(echo "scale=2; $W5_OK / $W5_DUR" | bc)
W5_MBPS=$(echo "scale=2; (($W5_G_OK+$W5_P_OK)*4096) / $W5_DUR / 1024 / 1024" | bc)
W5_AVG=$(awk '$1==200{sum+=$2;n++} END{if(n==0){print "N/A"} else {printf "%.6f", sum/n}}' /tmp/w5v3_low.txt)
W5_P50=$(awk '$1==200{print $2}' /tmp/w5v3_low.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.50)+1; printf "%.6f", a[idx]}}')
W5_P95=$(awk '$1==200{print $2}' /tmp/w5v3_low.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.95)+1; printf "%.6f", a[idx]}}')
W5_P99=$(awk '$1==200{print $2}' /tmp/w5v3_low.txt | sort -n | awk '{a[NR]=$1} END{if(NR==0){print "N/A"} else {idx=int((NR-1)*0.99)+1; printf "%.6f", a[idx]}}')
printf 'W5 MIX_70G30P_4K_LOW bucket=%s success=%s/%s (G_ok=%s,P_ok=%s) duration=%ss ops=%s mbps=%s avg=%ss p50=%ss p95=%ss p99=%ss\n' "$W5_BUCKET" "$W5_OK" "$W5_ITER" "$W5_G_OK" "$W5_P_OK" "$W5_DUR" "$W5_OPS" "$W5_MBPS" "$W5_AVG" "$W5_P50" "$W5_P95" "$W5_P99" | tee -a /tmp/realistic_perf_results_v3.txt
stop_server
