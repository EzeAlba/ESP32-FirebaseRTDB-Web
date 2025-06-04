// Firebase configuration
const firebaseConfig = {
    apiKey: "AIzaSyDKv1wkflxPSIm-3GBX5__qyY5US3WFI9o",
    authDomain: "esp32-basico-lectura-c3a63.firebaseapp.com",
    databaseURL: "https://esp32-basico-lectura-c3a63-default-rtdb.firebaseio.com",
    projectId: "esp32-basico-lectura-c3a63",
    storageBucket: "esp32-basico-lectura-c3a63.firebasestorage.app",
    messagingSenderId: "477555245069",
    appId: "1:477555245069:web:05c976803e53d0e7e21544"
};

// Initialize Firebase
const app = firebase.initializeApp(firebaseConfig);
const database = firebase.database();
const auth = firebase.auth();

// DOM Elements
const loginModal = new bootstrap.Modal(document.getElementById('loginModal'));
const loginBtn = document.getElementById('loginBtn');
const logoutBtn = document.getElementById('logoutBtn');
const mainApp = document.getElementById('mainApp');
const connectionStatus = document.getElementById('connectionStatus');

// Database References
const commandsRef = database.ref('buildings/Casa/comand');
const cardsRef = database.ref('buildings/Casa/cards');
const logsRef = database.ref('buildings/Casa/logs');
const serialRef = database.ref('buildings/Casa/Serial');
const memoryRef = database.ref('buildings/Casa/Memory');
const systemRef = database.ref('buildings/Casa/system');

// Initialize the application
function initApp() {
    // Show login modal if not authenticated
    auth.onAuthStateChanged(user => {
        if (user) {
            // User is signed in
            mainApp.classList.remove('d-none');
            loginModal.hide();
            setupDatabaseListeners();
        } else {
            // No user signed in
            mainApp.classList.add('d-none');
            loginModal.show();
        }
    });

    // Login button event
    loginBtn.addEventListener('click', () => {
        const email = document.getElementById('loginEmail').value;
        const password = document.getElementById('loginPassword').value;

        auth.signInWithEmailAndPassword(email, password)
            .catch(error => {
                document.getElementById('loginAlert').classList.remove('d-none');
                document.getElementById('loginAlert').textContent = error.message;
            });
    });

    // Logout button event
    logoutBtn.addEventListener('click', () => {
        auth.signOut();
    });
}

// Setup all database listeners
function setupDatabaseListeners() {
    // Connection status listener
    const connectedRef = database.ref('.info/connected');
    connectedRef.on('value', (snap) => {
        if (snap.val() === true) {
            connectionStatus.textContent = 'CONNECTED';
            connectionStatus.classList.remove('bg-secondary', 'bg-danger');
            connectionStatus.classList.add('bg-success');
        } else {
            connectionStatus.textContent = 'DISCONNECTED';
            connectionStatus.classList.remove('bg-secondary', 'bg-success');
            connectionStatus.classList.add('bg-danger');
        }
    });

    // Live access log listener
    serialRef.on('child_added', (snapshot) => {
        const logEntry = snapshot.val();
        const logElement = document.getElementById('liveLog');
        logElement.innerHTML += `<div>${new Date().toLocaleTimeString()}: ${logEntry}</div>`;
        logElement.scrollTop = logElement.scrollHeight;
    });

    // Cards table listener
    cardsRef.on('value', (snapshot) => {
        const tbody = document.getElementById('cardsTableBody');
        tbody.innerHTML = '';

        snapshot.forEach((cardSnapshot) => {
            const cardData = cardSnapshot.val();
            const row = document.createElement('tr');

            row.innerHTML = `
          <td>${cardData.uid || cardSnapshot.key}</td>
          <td>${cardData.user || 'Unknown'}</td>
          <td>
            <button class="btn btn-sm btn-danger delete-card-btn" data-uid="${cardSnapshot.key}">
              Delete
            </button>
          </td>
        `;

            tbody.appendChild(row);
        });

        // Add event listeners to delete buttons
        document.querySelectorAll('.delete-card-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const cardUid = e.target.getAttribute('data-uid');
                deleteCard(cardUid);
            });
        });
    });

    // Access history listener
    logsRef.on('value', (snapshot) => {
        const tbody = document.getElementById('accessTableBody');
        tbody.innerHTML = '';

        snapshot.forEach((cardSnapshot) => {
            const cardData = cardSnapshot.val();
            const row = document.createElement('tr');

            row.innerHTML = `
          <td>${cardData.time || 'N/A'}</td>
          <td>${cardData.uid || cardSnapshot.key}</td>
          <td>${cardData.user || 'Unknown'}</td>
          <td class="${cardData.access === 'Granted' ? 'text-success' : 'text-danger'}">
            ${cardData.access || 'Denied'}
          </td>
        `;

            tbody.appendChild(row);
        });
    });

    // System status listener
    systemRef.on('value', (snapshot) => {
        const systemData = snapshot.val() || {};
        document.getElementById('deviceStatus').textContent = systemData.status || 'Unknown';
        document.getElementById('lastSync').textContent = systemData.lastSync || '-';
        document.getElementById('totalCards').textContent = systemData.totalCards || '0';
    });
}

// Register new card
document.getElementById('registerCardBtn').addEventListener('click', () => {
    const userName = document.getElementById('newUserName').value;
    if (!userName) {
        alert('Please enter a user name');
        return;
    }

    // Set command to ESP32 to start card registration
    commandsRef.update({
        '01': 1, // Add card mode
        'newUserName': userName
    });

    document.getElementById('scanStatus').innerHTML = `
      <div class="alert alert-info">Waiting for card scan on device...</div>
    `;

    // Listen for completion
    const listener = commandsRef.child('01').on('value', (snap) => {
        if (snap.val() === 0) {
            document.getElementById('scanStatus').innerHTML = `
          <div class="alert alert-success">Card registration completed!</div>
        `;
            commandsRef.child('01').off('value', listener);
        }
    });
});

// Delete card
function deleteCard(cardUid) {
    if (confirm(`Are you sure you want to delete card ${cardUid}?`)) {
        // Set command to ESP32 to delete card
        commandsRef.update({
            '01': 3, // Delete card mode
            'deleteUser': cardUid
        });
    }
}

// System commands
document.getElementById('syncNowBtn').addEventListener('click', () => {
    commandsRef.update({
        '01': 2 // Sync mode
    });
});

document.getElementById('rebootBtn').addEventListener('click', () => {
    if (confirm('Are you sure you want to reboot the device?')) {
        systemRef.update({
            'command': 'reboot'
        });
    }
});

document.getElementById('factoryResetBtn').addEventListener('click', () => {
    if (confirm('WARNING: This will erase all data. Are you sure?')) {
        systemRef.update({
            'command': 'factory_reset'
        });
    }
});

// Refresh cards list
document.getElementById('refreshCardsBtn').addEventListener('click', () => {
    commandsRef.update({
        '01': 2 // View memory mode
    });
});

// Initialize the application when DOM is loaded
document.addEventListener('DOMContentLoaded', initApp);