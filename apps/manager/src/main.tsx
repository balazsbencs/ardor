import React from "react";
import ReactDOM from "react-dom/client";
import "@fontsource/saira/latin-400.css";
import "@fontsource/saira/latin-600.css";
import "@fontsource/saira-condensed/latin-500.css";
import "@fontsource/saira-condensed/latin-600.css";
import App from "./App";
import "./styles.css";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
