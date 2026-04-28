// holidays-manager.js - Centralized holiday date management
(function() {
    let holidaysData = {};
    let holidaysLoaded = false;

    // Load holidays from days.ini
    async function loadHolidays() {
        try {
            const response = await fetch('ini/days.ini');
            if (!response.ok) throw new Error('INI file not found');

            const iniContent = await response.text();
            parseIniContent(iniContent);
            holidaysLoaded = true;
        } catch (error) {
            console.warn('Using fallback holidays data:', error.message);
            // Fallback to hardcoded data if INI file not available
            holidaysData = {
                'Saint_Patrick': '03-16 to 03-18',
                'Easter_Week': '04-13 to 04-20',
                'Halloween': '10-01 to 10-31',
                'Christmas': '12-01 to 12-25',
                'New_Year': '12-26 to 01-01'
            };
        }
    }

    // Parse INI content
    function parseIniContent(content) {
        const lines = content.split('\n');
        let holidaysSection = false;

        for (const line of lines) {
            const trimmedLine = line.trim();
            if (trimmedLine === '[Holidays]') {
                holidaysSection = true;
                continue;
            }

            if (holidaysSection && trimmedLine && !trimmedLine.startsWith(';') && trimmedLine.includes('=')) {
                const [key, value] = trimmedLine.split('=').map(part => part.trim());
                holidaysData[key] = value;
            }
        }
    }

    // Check if current date is within a holiday period
    function isHoliday(holidayName) {
        if (!holidaysLoaded) {
            console.warn('Holidays data not loaded yet');
            return false;
        }

        const dateRange = holidaysData[holidayName];
        if (!dateRange) return false;

        const now = new Date();
        const currentMonth = now.getMonth() + 1;
        const currentDay = now.getDate();
        const currentYear = now.getFullYear();

        const [start, end] = dateRange.split('to').map(s => s.trim());
        const [startMonth, startDay] = start.split('-').map(Number);
        const [endMonth, endDay] = end.split('-').map(Number);

        let startDate, endDate;

        if (endMonth < startMonth) { // Crosses year boundary (e.g., New Year)
            startDate = new Date(currentYear, startMonth - 1, startDay);
            endDate = new Date(currentYear + 1, endMonth - 1, endDay);
            if (now < startDate) {
                startDate = new Date(currentYear - 1, startMonth - 1, startDay);
                endDate = new Date(currentYear, endMonth - 1, endDay);
            }
        } else {
            startDate = new Date(currentYear, startMonth - 1, startDay);
            endDate = new Date(currentYear, endMonth - 1, endDay);
        }

        return now >= startDate && now <= endDate;
    }

    // Expose functions globally
    window.HolidaysManager = {
        loadHolidays,
        isHoliday,
        getHolidaysData: () => holidaysData
    };
})();