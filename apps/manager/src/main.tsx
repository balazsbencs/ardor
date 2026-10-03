import React from "react";
import ReactDOM from "react-dom/client";
import "@fontsource/saira/latin-400.css";
import "@fontsource/saira/latin-500.css";
import "@fontsource/saira/latin-600.css";
import "@fontsource/saira-condensed/latin-500.css";
import "@fontsource/saira-condensed/latin-600.css";
import "@fontsource/saira-condensed/latin-700.css";
import "@fontsource/saira-condensed/latin-800.css";
import "@fontsource/jetbrains-mono/latin-500.css";
// The legacy base styles load before App so component stylesheets (Lamp Black) win equal-specificity ties.
import "./ui/tokens.css";
import "./styles.css";
import App from "./App";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
