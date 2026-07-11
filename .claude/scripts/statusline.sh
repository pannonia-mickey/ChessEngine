#!/bin/bash
# Előfeltétel: jq telepítve kell legyen
# winget install jqlang.jq   (Windows)
# brew install jq            (macOS)
# apt install jq             (Linux)

# ANSI Colors
readonly RED='\033[31m'
readonly GREEN='\033[32m'
readonly YELLOW='\033[33m'
readonly BLUE='\033[34m'
readonly CYAN='\033[36m'
readonly MAGENTA='\033[35m'
readonly BOLD='\033[1m'
readonly RESET='\033[0m'

# Read JSON data that Claude Code sends to stdin
input=$(cat)

# Extract fields using jq
MODEL=$(        jq -r '.model.display_name'                                                   <<< "$input")
EFFORT=$(       jq -r '.effort.level                                             // "medium"' <<< "$input" | tr '[:upper:]' '[:lower:]')
THINKING=$(     jq -r '.thinking.enabled                                         // false'    <<< "$input")

DIR=$(          jq -r '.workspace.current_dir                                    // "."'      <<< "$input")
MAX_CTX=$(      jq -r '.context_window.context_window_size                       // 200000'   <<< "$input")
PCT=$(          jq -r '.context_window.used_percentage                           // 0'        <<< "$input" | awk '{printf "%.0f", $1}')

FIVE_H_PCT=$(   jq -r '.rate_limits.five_hour.used_percentage                    // 0'        <<< "$input" | awk '{printf "%.0f", $1}')
SEVEN_D_PCT=$(  jq -r '.rate_limits.seven_day.used_percentage                    // 0'        <<< "$input" | awk '{printf "%.0f", $1}')
FIVE_H_RESET=$( jq -r '.rate_limits.five_hour.resets_at                          // 0'        <<< "$input")
SEVEN_D_RESET=$(jq -r '.rate_limits.seven_day.resets_at                          // 0'        <<< "$input")

CACHE_READ=$(   jq -r '.context_window.current_usage.cache_read_input_tokens     // 0'        <<< "$input")
CACHE_WRITE=$(  jq -r '.context_window.current_usage.cache_creation_input_tokens // 0'        <<< "$input")
COST=$(         jq -r '.cost.total_cost_usd                                      // 0'        <<< "$input")
DURATION_MS=$(  jq -r '.cost.total_duration_ms                                   // 0'        <<< "$input")

# Effort ikon és szín
case "$EFFORT" in
    "max"|"xhigh") 
        EFFORT_COLOR=$RED ;;
    "high") 
        EFFORT_COLOR=$YELLOW ;;
    "medium") 
        EFFORT_COLOR=$YELLOW ;;
    "low") 
        EFFORT_COLOR=$GREEN ;;
    *) 
        EFFORT_COLOR=$RESET ;;
esac
EFFORT_PART="${EFFORT_COLOR}${EFFORT}${RESET}"

# Thinking mode
if [ "$THINKING" = "true" ]; then
    THINKING_INFO="💭"
else
    THINKING_INFO=""
fi

# Rate Limits
RATE_LIMIT_INFO=""

if   [ "$FIVE_H_PCT" -ge 85 ]; then RL5_COLOR=$RED
elif [ "$FIVE_H_PCT" -ge 70 ]; then RL5_COLOR=$YELLOW
elif [ "$FIVE_H_PCT" -ge 50 ]; then RL5_COLOR=$YELLOW
else                                RL5_COLOR=$GREEN
fi

BAR_WIDTH=10
FILLED=$((FIVE_H_PCT * BAR_WIDTH / 100))
EMPTY=$((BAR_WIDTH - FILLED))
BAR=""
[ "$FILLED" -gt 0 ] && printf -v FILL "%${FILLED}s" && BAR="${FILL// /■}"
[ "$EMPTY" -gt 0 ] && printf -v PAD "%${EMPTY}s" && BAR="${BAR}${PAD// /□}"

RL5_PART="${RL5_COLOR}${BAR} ${FIVE_H_PCT}%${RESET}"

if [ "$FIVE_H_PCT" -ge 50 ]; then
	if command -v date >/dev/null; then
		if [[ "$OSTYPE" == "darwin"* ]]; then
			RESET_TIME=$(date -r "$FIVE_H_RESET" +"%H:%M" 2>/dev/null || echo "?")
		else
			RESET_TIME=$(date -d "@$FIVE_H_RESET" +"%H:%M" 2>/dev/null || echo "?")
		fi
		RL5_PART="${RL5_PART} → ${RESET_TIME}"
	fi
fi

if   [ "$SEVEN_D_PCT" -ge 75 ]; then RL7_COLOR=$RED
elif [ "$SEVEN_D_PCT" -ge 60 ]; then RL7_COLOR=$YELLOW
else                                  RL7_COLOR=$GREEN
fi

RL7_PART="${RL7_COLOR}${SEVEN_D_PCT}%${RESET}"

if [ "$SEVEN_D_PCT" -ge 60 ];  then
	if [[ "$OSTYPE" == "darwin"* ]]; then
		RESET7=$(date -r "$SEVEN_D_RESET" +"%B.%d %H:%M" 2>/dev/null || echo "?")
	else
		RESET7=$(date -d "@$SEVEN_D_RESET" +"%B.%d %H:%M" 2>/dev/null || echo "?")
	fi
	RL7_PART="${RL7_PART} → ${RESET7}"
fi

RATE_LIMIT_INFO="🔋 ${RL5_PART} / ${RL7_PART}"

# Git status
GIT_STATUS=""

if git rev-parse --git-dir > /dev/null 2>&1; then
    BRANCH=$(git branch --show-current 2>/dev/null)

    # Detached HEAD: show short commit hash instead of empty branch name
    IS_DETACHED=0
    if [ -z "$BRANCH" ]; then
        BRANCH=$(git rev-parse --short HEAD 2>/dev/null || echo "detached")
        IS_DETACHED=1
    fi

    TARGET=""
    case "$BRANCH" in
        feat/*|feature/*)     ICON="🌱" ; TARGET="develop" ;;
        fix/*|bugfix/*)       ICON="🐛" ; TARGET="develop" ;;
        perf/*|performance/*) ICON="⚡" ; TARGET="develop" ;;
        ref/*|refactor/*)     ICON="🧹" ; TARGET="develop" ;;
        test/*)               ICON="🧪" ; TARGET="develop" ;;
        rel/*|release/*)      ICON="🚀" ; TARGET="main" ;;
        hf/*|hotfix/*)        ICON="🔥" ; TARGET="main+develop" ;;
        dev|develop)          ICON="🌿" ;;
        main|master)          ICON="🏛️" ;;
        *)
			# Worktree detection (jobb és gyakoribb módszer)
			if git rev-parse --git-dir >/dev/null 2>&1; then
				if [ "$(git rev-parse --git-common-dir 2>/dev/null)" != "$(git rev-parse --git-dir 2>/dev/null)" ]; then
					ICON="🪹"      # linked worktree
					TARGET="develop"
				else
					ICON="🌳"      # main worktree vagy normál repo
				fi
			else
				ICON="🍂"          # nem git repo
			fi
        ;;
	esac
    
	# Override icon for detached HEAD after case (commit hash would match "*")
    [ "$IS_DETACHED" -eq 1 ] && ICON="🔍"

    # Előre/hátra commitok (csak ha van upstream tracking branch)
    AHEAD=0
    BEHIND=0
    if git rev-parse --abbrev-ref @{u} > /dev/null 2>&1; then
        AHEAD=$(git rev-list --count @{u}..HEAD 2>/dev/null || echo 0)
        BEHIND=$(git rev-list --count HEAD..@{u} 2>/dev/null || echo 0)
    fi

    SYNC=""
    [ "$AHEAD"  -gt 0 ] && SYNC=" ${CYAN}🡅 ${AHEAD}${RESET}"
    [ "$BEHIND" -gt 0 ] && SYNC="${SYNC} ${CYAN}🡇 ${BEHIND}${RESET}"

    # Állapot lekérése – X=index(staged), Y=working tree(unstaged)
    read SA SM SD SR WM WD U CONFLICT < <(git status --porcelain --untracked-files=all 2>/dev/null | awk '
        {
            x = substr($0, 1, 1)
            y = substr($0, 2, 1)
            if (x == "?" && y == "?")                                { u++;        next }
            if (x == "U" || y == "U" || (x == y && (x == "A" || x == "D"))) { conflict++; next }
            if (x == "A")             sa++
            if (x == "M")             sm++
            if (x == "D")             sd++
            if (x == "R" || x == "C") sr++
            if (y == "M")             wm++
            if (y == "D")             wd++
        }
        END { print sa+0, sm+0, sd+0, sr+0, wm+0, wd+0, u+0, conflict+0 }')

    # Staged blokk (commit-ready változások)
    STAGED=""
    [ "$SA" -gt 0 ] && STAGED="${STAGED} ${GREEN}+${SA}${RESET}"
    [ "$SM" -gt 0 ] && STAGED="${STAGED} ${YELLOW}~${SM}${RESET}"
    [ "$SD" -gt 0 ] && STAGED="${STAGED} ${RED}-${SD}${RESET}"
    [ "$SR" -gt 0 ] && STAGED="${STAGED} ${BLUE}r${SR}${RESET}"

    # Unstaged blokk (nem staged módosítások)
    UNSTAGED=""
    [ "$WM" -gt 0 ] && UNSTAGED="${UNSTAGED} ${YELLOW}~${WM}${RESET}"
    [ "$WD" -gt 0 ] && UNSTAGED="${UNSTAGED} ${RED}-${WD}${RESET}"

    # Összerakás: staged · unstaged  ?untracked  !conflict
    CHANGES="${STAGED} ·${UNSTAGED}"
    [ "$U"        -gt 0 ] && CHANGES="${CHANGES} ${CYAN}?${U}${RESET}"
    [ "$CONFLICT" -gt 0 ] && CHANGES="${CHANGES} ${RED}!${CONFLICT}${RESET}"

    if [ -n "$TARGET" ]; then
        GIT_STATUS="${ICON} ${BRANCH} → 🎯 ${TARGET} ${SYNC}${CHANGES}"
    else
        GIT_STATUS="${ICON} ${BRANCH}${SYNC}${CHANGES}"
    fi
fi

# Context window usage
USED_CTX_IN_K=$(( MAX_CTX * PCT / 100 / 1000 ))
MAX_CTX_IN_K=$(( MAX_CTX / 1000 ))
if (( PCT >= 75 )); then PCT_COLOR=$RED
elif (( PCT >= 50 )); then PCT_COLOR=$YELLOW
else                       PCT_COLOR=$GREEN
fi

BAR_WIDTH=10
FILLED=$((PCT * BAR_WIDTH / 100))
EMPTY=$((BAR_WIDTH - FILLED))
BAR=""
[ "$FILLED" -gt 0 ] && printf -v FILL "%${FILLED}s" && BAR="${FILL// /■}"
[ "$EMPTY" -gt 0 ] && printf -v PAD "%${EMPTY}s" && BAR="${BAR}${PAD// /□}"
#CONTEXT_INFO="📝 ${USED_CTX_IN_K}k/${MAX_CTX_IN_K}k (${PCT_COLOR}${PCT}%${RESET})"
CONTEXT_INFO="📝 ${PCT_COLOR}${BAR} ${PCT}%${RESET}"

CACHE_READ_K=$(( CACHE_READ / 1000 ))
CACHE_WRITE_K=$(( CACHE_WRITE / 1000 ))
CACHE_INFO="♻️ read ${CACHE_READ_K}k write ${CACHE_WRITE_K}k"

# Cost and duration tracking
COST_FMT=$(printf '$%.2f' "$COST")
MINS=$((DURATION_MS / 60000))
SECS=$(((DURATION_MS % 60000) / 1000))

# Output the status line - ${DIR##*/} extracts just the folder name
echo -e "🧠${THINKING_INFO} $MODEL ${EFFORT_PART} | 📁 ${DIR##*/} | ${GIT_STATUS}"
echo -e "${CONTEXT_INFO} | ${CACHE_INFO} | ${RATE_LIMIT_INFO} | 💰 ${COST_FMT} | ⏱️ ${MINS}m ${SECS}s"
