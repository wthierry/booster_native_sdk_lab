const K1_POSE_MODEL = {
  rootLink: "Trunk",
  links: {
    "Trunk": { visuals: [{"mesh": "Trunk", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.00637, -0.0, 0.063881], "size": [0.145968, 0.20506, 0.346763]}, {"mesh": "K1logo", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.073195, -0.000001, 0.177025], "size": [0.005005, 0.080442, 0.011582]}] },
    "Head_1": { visuals: [{"mesh": "Head_1", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.001705, 0.00215, 0.030499], "size": [0.059387, 0.0553, 0.060999]}] },
    "Head_2": { visuals: [{"mesh": "Head_2", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.000148, 0.0, 0.063394], "size": [0.127182, 0.160236, 0.182781]}] },
    "Left_Arm_1": { visuals: [{"mesh": "Left_Arm_1", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0055, 0.0565, -0.012761], "size": [0.065, 0.093, 0.071454]}] },
    "Left_Arm_2": { visuals: [{"mesh": "Left_Arm_2", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.000251, 0.007064, 0.0], "size": [0.063501, 0.084127, 0.069937]}] },
    "Left_Arm_3": { visuals: [{"mesh": "Left_Arm_3", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0029, 0.074821, -0.001252], "size": [0.0688, 0.153641, 0.065495]}] },
    "left_hand_link": { visuals: [{"mesh": "Left_Arm_4", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0, 0.099, 0.001992], "size": [0.06, 0.258, 0.066017]}] },
    "Right_Arm_1": { visuals: [{"mesh": "Right_Arm_1", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0055, -0.0565, -0.012761], "size": [0.065, 0.093, 0.071454]}] },
    "Right_Arm_2": { visuals: [{"mesh": "Right_Arm_2", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.000251, -0.007064, -0.0], "size": [0.063501, 0.084127, 0.069937]}] },
    "Right_Arm_3": { visuals: [{"mesh": "Right_Arm_3", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0029, -0.07482, -0.00125], "size": [0.0688, 0.153641, 0.065501]}] },
    "right_hand_link": { visuals: [{"mesh": "Right_Arm_4", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.0, -0.099, 0.001991], "size": [0.06, 0.258, 0.066017]}] },
    "Left_Hip_Pitch": { visuals: [{"mesh": "Left_Hip_Pitch", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0115, 0.0005, -0.02049], "size": [0.076, 0.074, 0.085964]}] },
    "Left_Hip_Roll": { visuals: [{"mesh": "Left_Hip_Roll", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.012, 0.0, -0.0095], "size": [0.077, 0.076922, 0.076]}] },
    "Left_Hip_Yaw": { visuals: [{"mesh": "Left_Hip_Yaw", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.010652, 0.0, -0.08149], "size": [0.098685, 0.076987, 0.16298]}] },
    "Left_Shank": { visuals: [{"mesh": "Left_Shank", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.006927, 0.000927, -0.111868], "size": [0.095904, 0.076886, 0.281644]}] },
    "Left_Ankle_Cross": { visuals: [{"mesh": "Left_Ankle_Cross", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.0, 0.0, 0.0], "size": [0.034, 0.018, 0.015]}] },
    "left_foot_link": { visuals: [{"mesh": "Left_Foot", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.026803, 0.001001, -0.006367], "size": [0.185381, 0.080094, 0.063734]}] },
    "Right_Hip_Pitch": { visuals: [{"mesh": "Right_Hip_Pitch", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.0115, -0.0005, -0.02049], "size": [0.076, 0.074, 0.085964]}] },
    "Right_Hip_Roll": { visuals: [{"mesh": "Right_Hip_Roll", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.012, 0.0, -0.0095], "size": [0.077, 0.076922, 0.076]}] },
    "Right_Hip_Yaw": { visuals: [{"mesh": "Right_Hip_Yaw", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [-0.010652, 0.0, -0.08149], "size": [0.098685, 0.076987, 0.16298]}] },
    "Right_Shank": { visuals: [{"mesh": "Right_Shank", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.006927, -0.000927, -0.111867], "size": [0.095904, 0.076886, 0.281643]}] },
    "Right_Ankle_Cross": { visuals: [{"mesh": "Right_Ankle_Cross", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.0, 0.0, 0.0], "size": [0.034, 0.018, 0.015]}] },
    "right_foot_link": { visuals: [{"mesh": "Right_Foot", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "center": [0.026797, -0.001001, -0.006367], "size": [0.185392, 0.080094, 0.063734]}] },
  },
  joints: [
    {"name": "AAHead_yaw", "parent": "Trunk", "child": "Head_1", "xyz": [0.0056, 0.0, 0.2149], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0]},
    {"name": "Head_pitch", "parent": "Head_1", "child": "Head_2", "xyz": [0.0, 0.0, 0.033], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "ALeft_Shoulder_Pitch", "parent": "Trunk", "child": "Left_Arm_1", "xyz": [0.0, 0.077, 0.1845], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Left_Shoulder_Roll", "parent": "Left_Arm_1", "child": "Left_Arm_2", "xyz": [0.0025, 0.068, -0.0135], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
    {"name": "Left_Elbow_Pitch", "parent": "Left_Arm_2", "child": "Left_Arm_3", "xyz": [0.0, 0.044428, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Left_Elbow_Yaw", "parent": "Left_Arm_3", "child": "left_hand_link", "xyz": [0.0, 0.1215, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0]},
    {"name": "ARight_Shoulder_Pitch", "parent": "Trunk", "child": "Right_Arm_1", "xyz": [0.0, -0.077, 0.1845], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Right_Shoulder_Roll", "parent": "Right_Arm_1", "child": "Right_Arm_2", "xyz": [0.0025, -0.068, -0.0135], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
    {"name": "Right_Elbow_Pitch", "parent": "Right_Arm_2", "child": "Right_Arm_3", "xyz": [0.0, -0.044428, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Right_Elbow_Yaw", "parent": "Right_Arm_3", "child": "right_hand_link", "xyz": [0.0, -0.1215, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0]},
    {"name": "Left_Hip_Pitch", "parent": "Trunk", "child": "Left_Hip_Pitch", "xyz": [0.0, 0.096, -0.077], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Left_Hip_Roll", "parent": "Left_Hip_Pitch", "child": "Left_Hip_Roll", "xyz": [0.0, 0.0, -0.026], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
    {"name": "Left_Hip_Yaw", "parent": "Left_Hip_Roll", "child": "Left_Hip_Yaw", "xyz": [0.012, 0.0, -0.0485], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0]},
    {"name": "Left_Knee_Pitch", "parent": "Left_Hip_Yaw", "child": "Left_Shank", "xyz": [-0.014, 0.0, -0.117], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Left_Ankle_Pitch", "parent": "Left_Shank", "child": "Left_Ankle_Cross", "xyz": [0.000197, 0.0002, -0.24519], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Left_Ankle_Roll", "parent": "Left_Ankle_Cross", "child": "left_foot_link", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
    {"name": "Right_Hip_Pitch", "parent": "Trunk", "child": "Right_Hip_Pitch", "xyz": [0.0, -0.096, -0.077], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Right_Hip_Roll", "parent": "Right_Hip_Pitch", "child": "Right_Hip_Roll", "xyz": [0.0, 0.0, -0.026], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
    {"name": "Right_Hip_Yaw", "parent": "Right_Hip_Roll", "child": "Right_Hip_Yaw", "xyz": [0.012, 0.0, -0.0485], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0]},
    {"name": "Right_Knee_Pitch", "parent": "Right_Hip_Yaw", "child": "Right_Shank", "xyz": [-0.014, 0.0, -0.117], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Right_Ankle_Pitch", "parent": "Right_Shank", "child": "Right_Ankle_Cross", "xyz": [0.000197, -0.0002, -0.24519], "rpy": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0]},
    {"name": "Right_Ankle_Roll", "parent": "Right_Ankle_Cross", "child": "right_foot_link", "xyz": [0.0, 0.0, 0.0], "rpy": [0.0, 0.0, 0.0], "axis": [1.0, 0.0, 0.0]},
  ],
};

(() => {
  const MODULES_PROMISE = Promise.all([
    import("/vendor/three.module.js"),
    import("/vendor/OrbitControls.js"),
    import("/vendor/STLLoader.js"),
  ]);

  function normalizeJointName(name) {
    return String(name || "").toLowerCase().replace(/[^a-z0-9]/g, "");
  }

  function linkColor(linkName) {
    if (linkName.includes("Head")) {
      return 0x9eb8d8;
    }
    if (linkName.includes("Arm") || linkName.includes("hand")) {
      return 0xd68b63;
    }
    if (linkName.includes("Hip") || linkName.includes("Shank") || linkName.includes("foot") || linkName.includes("Ankle")) {
      return 0x6cc1b4;
    }
    return 0xe7dcc9;
  }

  function meshPath(meshName) {
    return `/robot-assets/robots/K1/meshes/${encodeURIComponent(meshName)}.STL`;
  }

  function create(model, elements) {
    const { canvas, statusElement, metaElement, controllerElement } = elements;
    if (!canvas) {
      return { update() {}, setUseMeshes() {} };
    }

    const knownMeshNames = Array.from(new Set(
      Object.values(model.links)
        .flatMap((link) => link.visuals || [])
        .map((visual) => visual.mesh)
        .filter(Boolean),
    ));

    let latestState = null;
    let useMeshes = Boolean(controllerElement?.checked);
    let ready = false;

    let THREE = null;
    let OrbitControls = null;
    let STLLoader = null;
    let renderer = null;
    let scene = null;
    let camera = null;
    let controls = null;
    let loader = null;
    let frameHandle = null;

    const meshCache = new Map();
    const linkGroups = new Map();
    const jointBindings = [];
    const visualBindings = [];
    const fallbackBoxGeometries = new Map();

    function meshStateSummary() {
      const stats = { loaded: 0, loading: 0, error: 0, renderedTriangles: 0 };
      for (const entry of meshCache.values()) {
        if (entry.status === "loaded" && entry.geometry) {
          stats.loaded += 1;
          stats.renderedTriangles += Number(entry.triangleCount || 0);
        } else if (entry.status === "error") {
          stats.error += 1;
        } else {
          stats.loading += 1;
        }
      }
      return stats;
    }

    function updateStatus() {
      const available = Boolean(latestState?.available);
      const running = Boolean(latestState?.running);
      const state = String(latestState?.state || "waiting");
      const liveLabel = !ready ? "Loading" : available ? "Live" : running ? "Starting" : "Waiting";
      statusElement.textContent = liveLabel;
      statusElement.classList.toggle("backend-status-idle", liveLabel !== "Live");

      const updatedAt = latestState?.updated_at ? `Updated ${latestState.updated_at}` : "Waiting for joint state";
      const jointCount = Number(latestState?.joint_count || latestState?.names?.length || 0);
      const meshStats = meshStateSummary();
      const modeLabel = useMeshes
        ? `STL mode ${meshStats.loaded}/${knownMeshNames.length} loaded`
        : "Proxy mode";
      const details = [];
      if (useMeshes && meshStats.renderedTriangles) {
        details.push(`${meshStats.renderedTriangles} tris`);
      }
      if (useMeshes && meshStats.loading) {
        details.push(`${meshStats.loading} loading`);
      }
      if (useMeshes && meshStats.error) {
        details.push(`${meshStats.error} failed`);
      }
      const suffix = details.length ? ` • ${details.join(" • ")}` : "";
      metaElement.textContent =
        `${updatedAt} • ${jointCount} joints • state=${state} • ${modeLabel}${suffix}. Orbit: drag, zoom: wheel, pan: right-drag.`;
    }

    function getBoxGeometry(size) {
      const key = size.join(",");
      if (!fallbackBoxGeometries.has(key)) {
        fallbackBoxGeometries.set(key, new THREE.BoxGeometry(size[0], size[1], size[2]));
      }
      return fallbackBoxGeometries.get(key);
    }

    function ensureLinkGroup(name) {
      if (!linkGroups.has(name)) {
        const group = new THREE.Group();
        group.name = name;
        linkGroups.set(name, group);
      }
      return linkGroups.get(name);
    }

    function applyJointAngles() {
      if (!ready) {
        return;
      }
      const angles = {};
      const names = Array.isArray(latestState?.names) ? latestState.names : [];
      const positions = Array.isArray(latestState?.position) ? latestState.position : [];
      for (let index = 0; index < names.length; index += 1) {
        angles[normalizeJointName(names[index])] = Number(positions[index] || 0);
      }

      for (const binding of jointBindings) {
        const angle = angles[normalizeJointName(binding.name)] || 0;
        binding.axisGroup.quaternion.setFromAxisAngle(binding.axis, angle);
      }
    }

    function applyMeshVisibility() {
      for (const binding of visualBindings) {
        const entry = meshCache.get(binding.meshName);
        const canUseMesh = useMeshes && entry?.status === "loaded" && binding.meshInstance;
        if (binding.fallbackMesh) {
          binding.fallbackMesh.visible = !canUseMesh;
        }
        if (binding.meshInstance) {
          binding.meshInstance.visible = canUseMesh;
        }
      }
    }

    function attachLoadedMeshes(meshName) {
      const entry = meshCache.get(meshName);
      if (!entry || entry.status !== "loaded" || !entry.geometry) {
        return;
      }
      for (const binding of visualBindings) {
        if (binding.meshName !== meshName || binding.meshInstance) {
          continue;
        }
        const mesh = new THREE.Mesh(entry.geometry, binding.meshMaterial);
        mesh.castShadow = false;
        mesh.receiveShadow = false;
        binding.visualGroup.add(mesh);
        binding.meshInstance = mesh;
      }
      applyMeshVisibility();
    }

    function requestMesh(meshName) {
      if (!ready || !meshName || meshCache.has(meshName)) {
        return;
      }
      const entry = { status: "loading", geometry: null, triangleCount: 0, error: "" };
      meshCache.set(meshName, entry);
      loader.loadAsync(meshPath(meshName))
        .then((geometry) => {
          geometry.computeBoundingBox();
          geometry.computeVertexNormals();
          entry.status = "loaded";
          entry.geometry = geometry;
          entry.triangleCount = Math.floor((geometry.attributes.position?.count || 0) / 3);
          attachLoadedMeshes(meshName);
          updateStatus();
        })
        .catch((error) => {
          entry.status = "error";
          entry.error = error?.message || "mesh load failed";
          updateStatus();
        });
    }

    function ensureMeshRequests() {
      for (const meshName of knownMeshNames) {
        requestMesh(meshName);
      }
    }

    function resizeRenderer() {
      if (!renderer || !camera) {
        return;
      }
      const rect = canvas.getBoundingClientRect();
      const width = Math.max(1, Math.round(rect.width));
      const height = Math.max(1, Math.round(rect.height));
      renderer.setPixelRatio(window.devicePixelRatio || 1);
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
    }

    function renderFrame() {
      if (!renderer || !scene || !camera || !controls) {
        return;
      }
      controls.update();
      renderer.render(scene, camera);
      frameHandle = window.requestAnimationFrame(renderFrame);
    }

    function buildViewer() {
      scene = new THREE.Scene();
      scene.background = null;
      scene.up.set(0, 0, 1);

      renderer = new THREE.WebGLRenderer({
        canvas,
        antialias: true,
        alpha: true,
        powerPreference: "high-performance",
      });
      renderer.outputColorSpace = THREE.SRGBColorSpace;
      renderer.setClearAlpha(0);

      camera = new THREE.PerspectiveCamera(35, 1, 0.01, 10);
      camera.up.set(0, 0, 1);
      camera.position.set(1.2, -1.6, 0.95);

      controls = new OrbitControls(camera, canvas);
      controls.enableDamping = true;
      controls.dampingFactor = 0.08;
      controls.screenSpacePanning = true;
      controls.target.set(0.0, 0.0, 0.05);
      controls.minDistance = 0.35;
      controls.maxDistance = 3.5;
      controls.maxPolarAngle = Math.PI * 0.95;

      loader = new STLLoader();

      const ambient = new THREE.HemisphereLight(0xf2e8db, 0x34261e, 1.65);
      scene.add(ambient);

      const keyLight = new THREE.DirectionalLight(0xfff0d9, 2.2);
      keyLight.position.set(1.6, -1.4, 2.4);
      scene.add(keyLight);

      const fillLight = new THREE.DirectionalLight(0x9bc3ff, 1.25);
      fillLight.position.set(-1.8, 1.2, 1.1);
      scene.add(fillLight);

      const rimLight = new THREE.DirectionalLight(0xffb06f, 0.65);
      rimLight.position.set(-1.1, -2.1, 0.9);
      scene.add(rimLight);

      const grid = new THREE.GridHelper(2.2, 22, 0x7e604a, 0x4a3529);
      grid.rotation.x = Math.PI / 2;
      grid.position.z = -0.42;
      grid.material.opacity = 0.35;
      grid.material.transparent = true;
      scene.add(grid);

      const rootGroup = new THREE.Group();
      rootGroup.rotation.x = 0;
      scene.add(rootGroup);

      for (const linkName of Object.keys(model.links)) {
        ensureLinkGroup(linkName);
      }

      rootGroup.add(ensureLinkGroup(model.rootLink));

      for (const joint of model.joints) {
        const parentGroup = ensureLinkGroup(joint.parent);
        const childGroup = ensureLinkGroup(joint.child);
        const originGroup = new THREE.Group();
        originGroup.position.fromArray(joint.xyz);
        originGroup.rotation.set(joint.rpy[0], joint.rpy[1], joint.rpy[2], "XYZ");
        const axisGroup = new THREE.Group();
        originGroup.add(axisGroup);
        axisGroup.add(childGroup);
        parentGroup.add(originGroup);
        jointBindings.push({
          name: joint.name,
          axis: new THREE.Vector3(joint.axis[0], joint.axis[1], joint.axis[2]).normalize(),
          axisGroup,
        });
      }

      for (const [linkName, link] of Object.entries(model.links)) {
        const linkGroup = ensureLinkGroup(linkName);
        const color = linkColor(linkName);
        for (const visual of link.visuals || []) {
          const visualGroup = new THREE.Group();
          visualGroup.position.fromArray(visual.xyz);
          visualGroup.rotation.set(visual.rpy[0], visual.rpy[1], visual.rpy[2], "XYZ");
          linkGroup.add(visualGroup);

          const fallbackMesh = new THREE.Mesh(
            getBoxGeometry(visual.size),
            new THREE.MeshStandardMaterial({
              color,
              roughness: 0.72,
              metalness: 0.08,
            }),
          );
          fallbackMesh.position.set(visual.center[0], visual.center[1], visual.center[2]);
          visualGroup.add(fallbackMesh);

          visualBindings.push({
            meshName: visual.mesh,
            visualGroup,
            fallbackMesh,
            meshInstance: null,
            meshMaterial: new THREE.MeshStandardMaterial({
              color,
              roughness: 0.6,
              metalness: 0.12,
            }),
          });
        }
      }

      resizeRenderer();
      ready = true;
      applyJointAngles();
      if (useMeshes) {
        ensureMeshRequests();
      }
      applyMeshVisibility();
      updateStatus();
      if (frameHandle) {
        window.cancelAnimationFrame(frameHandle);
      }
      frameHandle = window.requestAnimationFrame(renderFrame);
    }

    MODULES_PROMISE
      .then(([threeModule, orbitControlsModule, stlLoaderModule]) => {
        THREE = threeModule;
        OrbitControls = orbitControlsModule.OrbitControls;
        STLLoader = stlLoaderModule.STLLoader;
        buildViewer();
      })
      .catch((error) => {
        statusElement.textContent = "Error";
        statusElement.classList.add("backend-status-idle");
        metaElement.textContent = `3D viewport failed to load: ${error?.message || error}`;
      });

    window.addEventListener("resize", resizeRenderer);
    if (typeof ResizeObserver === "function") {
      new ResizeObserver(resizeRenderer).observe(canvas);
    }

    function update(state) {
      latestState = state || null;
      applyJointAngles();
      updateStatus();
    }

    function setUseMeshes(nextValue) {
      useMeshes = Boolean(nextValue);
      if (controllerElement) {
        controllerElement.checked = useMeshes;
      }
      if (useMeshes) {
        ensureMeshRequests();
      }
      applyMeshVisibility();
      updateStatus();
    }

    update({ available: false, running: false, state: "waiting", names: [], position: [] });
    return { update, setUseMeshes };
  }

  window.BoosterPoseViewer = {
    create({ canvas, statusElement, metaElement, controllerElement }) {
      return create(K1_POSE_MODEL, { canvas, statusElement, metaElement, controllerElement });
    },
  };
})();
