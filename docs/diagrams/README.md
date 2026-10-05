# README diagrams

The main README embeds checked SVG images, so displaying it does not depend
on GitHub's Mermaid parser version. The editable `.mmd` files are the diagram
sources. Their corresponding `.svg` files are generated outputs.

To regenerate them, install Node.js and Mermaid CLI from the official npm
registry:

```sh
npm install --global @mermaid-js/mermaid-cli@11
mmdc -c docs/diagrams/mermaid-config.json -i docs/diagrams/system.mmd -o docs/diagrams/system.svg -b white
mmdc -c docs/diagrams/mermaid-config.json -i docs/diagrams/handshake.mmd -o docs/diagrams/handshake.svg -b white
mmdc -c docs/diagrams/mermaid-config.json -i docs/diagrams/led-states.mmd -o docs/diagrams/led-states.svg -b white
python3 test/verify_readme.py
```

Run the commands from the repository root. After changing a diagram source,
regenerate its image, inspect it, and run the README checks before committing.
Mermaid CLI needs its supported browser runtime for rendering.

Sequence diagram labels use words and commas rather than literal semicolons.
Mermaid treats semicolons as statement separators; this was the cause of the
previous sequence diagram parse error.
