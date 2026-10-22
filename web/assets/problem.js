import { element, requestJson, showDifficulty, validProblemId } from "./common.js";

const editor = document.getElementById("code-editor");
const reset = document.getElementById("reset-button");
const submit = document.getElementById("submit-button");
const submitLabel = document.getElementById("submit-label");
const pageState = document.getElementById("page-state");
const resultPanel = document.getElementById("result-panel");
const resultDetails = document.getElementById("result-details");
const resultIcon = document.getElementById("result-icon");
const resultMeta = document.getElementById("result-meta");
const staleNotice = document.getElementById("stale-notice");
const draftStatus = document.getElementById("draft-status");
const lineNumbers = document.getElementById("line-numbers");
const id = new URLSearchParams(window.location.search).get("id");
let problem = null;
let judging = false;
let lastSubmittedCode = null;

const outcomes = {
  ACCEPTED: ["AC", "通过", "代码通过了本题的判题测试。", "success"],
  WRONG_ANSWER: ["WA", "答案错误", "程序输出与标准答案不一致，请检查代码。", "error"],
  COMPILE_ERROR: ["CE", "编译错误", "代码未能编译，请查看下方编译信息。", "error"],
  TIME_LIMIT_EXCEEDED: ["TLE", "运行超时", "程序超过了题目规定的运行时间。", "error"],
  RUNTIME_ERROR: ["RE", "运行错误", "程序运行时异常退出，请检查代码。", "error"],
  OUTPUT_LIMIT_EXCEEDED: ["OLE", "输出超限", "程序输出超过了题目允许的文件大小。", "error"],
  SYSTEM_ERROR: ["ERROR", "判题异常", "判题过程遇到问题，请稍后再试。", "error"]
};

function updateLines() {
  const count = editor.value.split("\n").length;
  lineNumbers.textContent = Array.from({ length: count }, (_, index) => index + 1).join("\n");
  lineNumbers.scrollTop = editor.scrollTop;
}

function saveDraft() {
  if (!problem) return;
  try {
    const key = "oj:draft:v1:" + problem.id;
    if (editor.value === problem.starter_code) {
      localStorage.removeItem(key);
      draftStatus.textContent = "当前为初始代码";
    } else {
      localStorage.setItem(key, editor.value);
      draftStatus.textContent = "草稿已保存到当前浏览器";
    }
  } catch {
    draftStatus.textContent = "草稿仅保留在当前页面";
  }
}

function codeChanged() {
  updateLines();
  saveDraft();
  staleNotice.hidden = lastSubmittedCode === null || editor.value === lastSubmittedCode;
}

function showSummary(tone, code, title, message) {
  resultPanel.dataset.tone = tone;
  document.getElementById("result-code").textContent = code;
  document.getElementById("result-title").textContent = title;
  document.getElementById("result-message").textContent = message;
  resultIcon.replaceChildren();
  if (tone === "pending") {
    resultIcon.append(element("span", "spinner"));
  } else {
    resultIcon.textContent = tone === "success" ? "✓" : tone === "error" ? "!" : "—";
  }
}

function addOutput(title, text) {
  const block = element("div", "result-output");
  block.append(element("h4", "", title));
  const limit = 20000;
  block.append(element("pre", "", text ? text.slice(0, limit) : "（无输出）"));
  if (text.length > limit) {
    block.append(element("p", "truncation-note", "内容较长，仅显示前 " + limit + " 个字符。"));
  }
  resultDetails.append(block);
}

async function submitCode(event) {
  event.preventDefault();
  if (!problem || judging) return;
  if (!editor.value.trim()) {
    lastSubmittedCode = null;
    staleNotice.hidden = true;
    showSummary("error", "未提交", "请先编写代码", "代码不能为空，请在编辑区输入完整的 C++ 程序。");
    resultDetails.replaceChildren();
    resultMeta.hidden = true;
    editor.focus();
    return;
  }
  judging = true;
  lastSubmittedCode = editor.value;
  saveDraft();
  editor.disabled = true;
  reset.disabled = true;
  submit.disabled = true;
  submitLabel.textContent = "判题中…";
  resultPanel.setAttribute("aria-busy", "true");
  staleNotice.hidden = true;
  resultMeta.hidden = true;
  resultDetails.replaceChildren();
  showSummary("pending", "判题中", "正在判题", "正在编译并运行你的代码，请稍候。");
  try {
    const body = await requestJson("/submit", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ problem_id: problem.id, code: lastSubmittedCode })
    });
    if (body.problem_id !== problem.id || !Object.prototype.hasOwnProperty.call(outcomes, body.judge_status)) {
      throw new Error("判题结果格式异常，请稍后再试。");
    }
    const [code, title, message, tone] = outcomes[body.judge_status];
    showSummary(tone, code, title, message);
    addOutput("程序输出", typeof body.output === "string" ? body.output : "");
    if (body.compile_error) addOutput("编译信息", String(body.compile_error));
    if (body.runtime_error) addOutput("运行错误信息", String(body.runtime_error));
    if (body.judge_status === "RUNTIME_ERROR") {
      const details = [];
      if (Number.isInteger(body.exit_code)) details.push("退出码 " + body.exit_code);
      if (Number.isInteger(body.signal) && body.signal !== 0) details.push("信号 " + body.signal);
      resultMeta.textContent = details.join(" · ");
      resultMeta.hidden = details.length === 0;
    }
  } catch (error) {
    showSummary("error", "未获取结果", "未能完成判题", error.message);
  } finally {
    judging = false;
    editor.disabled = false;
    reset.disabled = false;
    submit.disabled = false;
    submitLabel.textContent = "提交代码";
    resultPanel.setAttribute("aria-busy", "false");
  }
}

function renderProblem(data) {
  document.title = data.title + " · OJ 练习场";
  document.getElementById("problem-title").textContent = data.title;
  document.getElementById("problem-number").textContent = "# " + data.id;
  showDifficulty(document.getElementById("problem-difficulty"), data.difficulty);
  document.getElementById("cpu-limit").textContent = "CPU 时间 " + data.limits.cpu_limit_sec + " s";
  document.getElementById("memory-limit").textContent = "内存 " + data.limits.mem_limit_mb + " MiB";
  document.getElementById("additional-limits").textContent = "实际运行时间上限 " + data.limits.wall_limit_sec +
    " s · 单个输出文件上限 " + data.limits.output_limit_mb + " MiB";
  document.getElementById("problem-description").textContent = data.description;
  const samples = document.getElementById("samples");
  samples.replaceChildren();
  for (const [index, sample] of data.samples.entries()) {
    const block = element("div", "sample");
    block.append(element("p", "sample-title", "样例 " + (index + 1)),
      element("p", "sample-label", "输入"), element("pre", "", sample.input || "（空输入）"),
      element("p", "sample-label", "输出"), element("pre", "", sample.output || "（空输出）"));
    samples.append(block);
  }
  if (data.samples.length === 0) samples.append(element("p", "muted", "本题暂无公开样例。"));
  editor.value = data.starter_code;
  try {
    const saved = localStorage.getItem("oj:draft:v1:" + data.id);
    if (saved !== null) {
      editor.value = saved;
      draftStatus.textContent = "已恢复本地草稿";
    } else {
      draftStatus.textContent = "当前为初始代码";
    }
  } catch {
    draftStatus.textContent = "草稿仅保留在当前页面";
  }
  updateLines();
  pageState.hidden = true;
  document.getElementById("workbench").hidden = false;
  editor.disabled = false;
  reset.disabled = false;
  submit.disabled = false;
}

async function loadProblem() {
  try {
    if (!validProblemId(id)) throw new Error("题目编号无效，请返回题库选择一道题目。");
    const body = await requestJson("/problems/" + encodeURIComponent(id));
    const data = body.problem;
    if (!data || data.id !== id || typeof data.title !== "string" || typeof data.description !== "string" ||
        typeof data.starter_code !== "string" || !data.limits || !Array.isArray(data.samples) ||
        data.samples.some(sample => !sample || typeof sample.input !== "string" || typeof sample.output !== "string") ||
        ["cpu_limit_sec", "mem_limit_mb", "wall_limit_sec", "output_limit_mb"].some(key =>
          !Number.isInteger(data.limits[key]) || data.limits[key] <= 0)) {
      throw new Error("题目数据格式异常，请稍后再试。");
    }
    problem = data;
    renderProblem(data);
  } catch (error) {
    pageState.classList.add("is-error");
    pageState.replaceChildren(element("p", "", error.message));
    const back = element("a", "", "返回题库");
    back.href = "/";
    const retry = element("button", "button button-secondary", "重新加载");
    retry.type = "button";
    retry.addEventListener("click", () => window.location.reload());
    pageState.append(back, retry);
  }
}

editor.addEventListener("input", codeChanged);
editor.addEventListener("scroll", () => { lineNumbers.scrollTop = editor.scrollTop; });
editor.addEventListener("keydown", event => {
  if (event.key === "Tab" && !event.shiftKey) {
    event.preventDefault();
    editor.setRangeText("    ", editor.selectionStart, editor.selectionEnd, "end");
    codeChanged();
  }
});
document.addEventListener("keydown", event => {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter" && problem && !judging) submitCode(event);
});
document.getElementById("submit-form").addEventListener("submit", submitCode);
reset.addEventListener("click", () => {
  if (!problem || judging) return;
  if (editor.value !== problem.starter_code && !window.confirm("要恢复初始代码吗？当前草稿将被替换。")) return;
  editor.value = problem.starter_code;
  codeChanged();
  editor.focus();
});
loadProblem();
