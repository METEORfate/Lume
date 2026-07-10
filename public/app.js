const button = document.querySelector("#ping");
const result = document.querySelector("#result");

button.addEventListener("click", () => {
  result.textContent = "JavaScript was served correctly.";
});
