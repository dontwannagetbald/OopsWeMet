export async function slide01(presentation) {
  const slide = presentation.slides.add({ title: "项目技术路线" });

  slide.background.fill = "#FBF4E8";

  const mermaid = String.raw`
flowchart LR
  T["项目技术路线"]:::title

  A["1. 人设配置层"]:::stage
  B["2. Prompt / Registry"]:::stage
  C["3. Agent 服务层"]:::core
  D["4. 输出编排层"]:::stage
  E["5. 终端硬件层"]:::stage

  T --> A --> B --> C --> D --> E

  A1["skills/<persona_id>"]:::tag
  A2["meta.json / SKILL.md / assets"]:::tag
  B1["Prompt Builder"]:::tag
  B2["PersonaRegistry"]:::tag
  C1["PersonaAgent"]:::key
  C2["FastAPI / CLI / Bridge / Duel"]:::tag
  C3["SessionStore / AgentPool"]:::tag
  D1["回复文本"]:::tag
  D2["情绪状态映射"]:::tag
  D3["TXT协议下发"]:::tag
  D4["拼音音素语音合成"]:::key
  E1["M5Stack / Arduino"]:::tag
  E2["动画场景 / PCM播放 / WebSocket"]:::tag
  E3["BLE Encounter / NFC 触发"]:::tag

  A --- A1
  A --- A2
  B --- B1
  B --- B2
  C --- C1
  C --- C2
  C --- C3
  D --- D1
  D --- D2
  D --- D3
  D --- D4
  E --- E1
  E --- E2
  E --- E3

  S["人设驱动 + 多入口服务 + 硬件联动"]:::summary
  E --> S

  classDef title fill:#D9472B,color:#FFFFFF,stroke:#D9472B,font-size:24px,font-weight:bold;
  classDef stage fill:#FFF8EF,color:#1E1A16,stroke:#C9A97B,stroke-width:2px,font-size:18px,font-weight:bold;
  classDef core fill:#2E2A27,color:#FFF4E8,stroke:#2E2A27,stroke-width:2px,font-size:18px,font-weight:bold;
  classDef tag fill:#F6E7D2,color:#5B4630,stroke:#D5B38C,font-size:12px;
  classDef key fill:#FFE0B8,color:#5A2A00,stroke:#D88A1F,stroke-width:2px,font-size:15px,font-weight:bold;
  classDef summary fill:#3A3530,color:#FFF4E8,stroke:#3A3530,font-size:14px,font-weight:bold;
`;

  await slide.fromMermaid(
    mermaid,
    { left: 84, top: 40, width: 1112, height: 620 },
    {
      theme: "neutral",
      backgroundColor: "#FBF4E8",
    },
  );

  return slide;
}
