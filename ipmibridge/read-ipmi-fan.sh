#!/bin/bash
echo "start"
IPMITOOL=${IPMITOOL:-/usr/bin/ipmitool}
IPMI_HOST=${IPMI_HOST:-}
IPMI_USER=${IPMI_USER:-}
IPMI_PASS=${IPMI_PASS:-}
HWMON_NAME=${HWMON_NAME:-hwmonsw}
TIMEOUT_RETRIES=${TIMEOUT_RETRIES:-50}

# build ipmitool cmd
if [ -n "$IPMI_HOST" ]; then
  AUTH_OPTS="-I lanplus -H $IPMI_HOST"
  [ -n "$IPMI_USER" ] && AUTH_OPTS+=" -U $IPMI_USER"
  [ -n "$IPMI_PASS" ] && AUTH_OPTS+=" -P $IPMI_PASS"
fi

RAW=""
if [ -n "$IPMI_HOST" ]; then
  CMD2="$IPMITOOL $AUTH_OPTS sdr type Fan"
else
  CMD2="$IPMITOOL sdr type Fan"
fi
#  echo "type Fan"
RAW=$($CMD2 2>/dev/null) || RAW=""
echo "$RAW"

OUT=""
while IFS= read -r line; do
  [ -z "$line" ] && continue

  # if line already in name=value or with leading CSV fields, join and remove leading numeric tokens
  # replace commas with spaces to split tokens
  toks=()
  #for t in $(echo "$line" | tr ',' ' '); do
  L=$(echo "$line" | awk -F'|' '{ if($3 !~ "ok") exit; n=$1; gsub(/ +/,"",n); n=tolower(n); rpm=$5; printf "%s=%d",n,rpm; exit }')
  echo "$L"
  toks+=("$L")

#  for t in $(echo "$line" | awk -F'|' '{n=$1; gsub(/^ +| +$/,"",n); gsub(/ +/,"",n); n=tolower(n); rpm=""; for(i=NF;i>=1;i--) if(match($i,/(o
#    toks+=("$t")
#  done


  # find first token that contains a letter -> that's the name token (skip numeric tokens like 000)
  name_token=""
  for t in "${toks[@]}"; do
    if echo "$t" | grep -q '[A-Za-z]'; then
      name_token="$t"
      break
    fi
  done
  # if name_token contains '=', it's already name=value
  if [ -n "$name_token" ] && echo "$name_token" | grep -q '='; then
    pair="$name_token"
  else
    # otherwise find value token (first token with digits) and form pair
    val_token=""
    for t in "${toks[@]}"; do
      if echo "$t" | grep -q '[0-9]'; then
        val_token="$t"
        break
      fi
    done
    [ -z "$name_token" ] && continue
    [ -z "$val_token" ] && continue
    # normalize name and value
    name=$(echo "$name_token" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/_/g' | sed -E 's/^_+|_+$//g')
    val=$(echo "$val_token" | grep -oE '[0-9]+' || true)
    [ -z "$val" ] && continue
    # ensure prefix fan
    if ! echo "$name" | grep -q '^fan'; then
      name="fan_${name}"
    fi
    pair="${name}=${val}"
  fi

  # if pair may have extra chars, sanitize: take left of = as name, right numeric part as value
  if echo "$pair" | grep -q '='; then
    n=$(echo "$pair" | cut -d= -f1)
    v=$(echo "$pair" | cut -d= -f2- | grep -oE '[0-9]+' || true)
    [ -z "$v" ] && continue
    n=$(echo "$n" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/_/g' | sed -E 's/^_+|_+$//g')
    if ! echo "$n" | grep -q '^fan'; then
      n="fan_${n}"
    fi
    pair="${n}=${v}"
  else
    continue
  fi

  if [ -z "$OUT" ]; then
    OUT="$pair"
  else
    OUT="${OUT},${pair}"
  fi
done <<EOF
$RAW
EOF

[ -z "$OUT" ] && exit 1

# find hwmon path
hwmon_path=""
retries=$TIMEOUT_RETRIES
while [ $retries -gt 0 ]; do
  for p in /sys/class/hwmon/hwmon*; do
    [ -e "$p/name" ] || continue
    content=$(cat "$p/name" 2>/dev/null || true)
    if [ "$content" = "$HWMON_NAME" ]; then
      hwmon_path="$p"
      break 2
    fi
  done
  sleep 0.2
  retries=$((retries-1))
done

echo "$hwmon_path"
[ -z "$hwmon_path" ] && { echo "hwmon '$HWMON_NAME' not found" >&2; exit 2; }

update_file="${hwmon_path}/update"
[ -w "$update_file" ] || { echo "update not writable: $update_file" >&2; exit 3; }

echo -n "$OUT" > "$update_file" 2>/dev/null || { echo "write failed" >&2; exit 4; }
echo "$OUT"
exit 0

