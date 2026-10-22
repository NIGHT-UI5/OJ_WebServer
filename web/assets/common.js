export function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

export function showDifficulty(node, value) {
  const labels = { easy: "简单", medium: "中等", hard: "困难" };
  const known = Object.prototype.hasOwnProperty.call(labels, value);
  node.className = "difficulty" + (known ? " " + value : "");
  node.textContent = known ? labels[value] : "未分级";
}

export function requestError(status) {
  const messages = {
    400: "请求内容有误，请检查题目与代码。",
    404: "没有找到这道题，请返回题库重新选择。",
    502: "暂时无法连接判题服务，请稍后再试。",
    503: "判题服务暂不可用，请稍后再试。",
    504: "等待判题结果超时，请稍后再试。"
  };
  return messages[status] || "服务暂时出现问题，请稍后再试。";
}

export async function requestJson(path, options = {}) {
  let response;
  try {
    response = await fetch(path, { cache: "no-store", ...options });
  } catch {
    throw new Error("连接中断或服务无法访问，请检查网络后再试。");
  }
  let body;
  try {
    body = await response.json();
  } catch {
    throw new Error(response.ok ? "服务返回的数据格式异常，请稍后再试。" : requestError(response.status));
  }
  if (!response.ok || !body || body.ok !== true) {
    throw new Error(requestError(response.status));
  }
  return body;
}

export function validProblemId(value) {
  return typeof value === "string" && value.trim() === value && /^[1-9][0-9]*$/.test(value);
}
