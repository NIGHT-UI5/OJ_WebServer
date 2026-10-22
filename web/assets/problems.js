import { element, requestJson, showDifficulty, validProblemId } from "./common.js";

const panel = document.getElementById("library-panel");
const state = document.getElementById("list-state");
const table = document.getElementById("problem-table-container");
const list = document.getElementById("problem-list");
const count = document.getElementById("problem-count");
const refresh = document.getElementById("refresh-button");
const template = document.getElementById("problem-row-template");

async function loadProblems() {
  refresh.disabled = true;
  panel.setAttribute("aria-busy", "true");
  state.hidden = false;
  state.classList.remove("is-error");
  state.replaceChildren(element("span", "spinner"), element("p", "", "正在加载题目…"));
  table.hidden = true;
  count.textContent = "正在加载";
  try {
    const body = await requestJson("/problems");
    if (!Array.isArray(body.problems) || body.problems.some(problem =>
      !problem || !validProblemId(problem.id) || typeof problem.title !== "string")) {
      throw new Error("题库数据格式异常，请稍后再试。");
    }
    list.replaceChildren();
    for (const problem of body.problems) {
      const row = template.content.cloneNode(true);
      row.querySelector(".problem-id").textContent = problem.id;
      const href = "/problem.html?id=" + encodeURIComponent(problem.id);
      const title = row.querySelector(".problem-link");
      title.href = href;
      title.textContent = problem.title;
      const start = row.querySelector(".start-link");
      start.href = href;
      start.setAttribute("aria-label", "开始做题：" + problem.title);
      showDifficulty(row.querySelector(".difficulty"), problem.difficulty);
      list.append(row);
    }
    count.textContent = "共 " + body.problems.length + " 道题";
    if (body.problems.length === 0) {
      state.replaceChildren(element("p", "", "题库暂时还没有题目。"));
    } else {
      state.hidden = true;
      table.hidden = false;
    }
  } catch (error) {
    count.textContent = "加载失败";
    state.classList.add("is-error");
    state.replaceChildren(element("p", "", error.message), element("p", "", "点击“刷新题目”重新加载。"));
  } finally {
    refresh.disabled = false;
    panel.setAttribute("aria-busy", "false");
  }
}

refresh.addEventListener("click", loadProblems);
loadProblems();
