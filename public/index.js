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

// Reference to your data
const cardsRef = database.ref('buildings/Casa/cards');

// Function to display access logs
function displayAccessLogs() {
  cardsRef.on('value', (snapshot) => {
    const accessTableBody = document.getElementById('accessTableBody');
    accessTableBody.innerHTML = ''; // Clear existing rows

    snapshot.forEach((childSnapshot) => {
      const cardData = childSnapshot.val();
      const row = document.createElement('tr');

      row.innerHTML = `
        <td>${cardData.time || 'N/A'}</td>
        <td>${cardData.uid || 'N/A'}</td>
        <td>${cardData.user || 'Unknown'}</td>
        <td class="${cardData.access === 'Granted' ? 'text-success' : 'text-danger'}">
          ${cardData.access || 'Denied'}
        </td>
      `;

      accessTableBody.appendChild(row);
    });
  });
}

// Initialize the display when page loads
window.addEventListener('load', () => {
  displayAccessLogs();
});